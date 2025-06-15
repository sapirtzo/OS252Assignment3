#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_CHILDREN 4

// Message header structure (32-bit total)
struct msg_header {
    uint16 child_index;
    uint16 msg_length;
};

// Align address to 4-byte boundary
uint64 align_addr(uint64 addr) {
    return (addr + 3) & ~3;
}

// Helper function to copy string and return length
int copy_string(char* dest, char* src) {
    int len = 0;
    while (src[len] != '\0') {
        dest[len] = src[len];
        len++;
    }
    dest[len] = '\0';
    return len;
}

// Child process function
void child_process(int child_index, char* buffer) {
    char message[128];
    int msg_count = 0;
    
    // Each child writes multiple messages of varying lengths
    int base_messages = 10;
    int extra_messages = (child_index == 0) ? 50 : 0; // Child 0 writes more to exceed buffer
    
    for (int i = 0; i < base_messages + extra_messages; i++) {
        // Create message with varying lengths
        int msg_len;
        if (i % 3 == 0) {
            char temp[64];
            // Manual string formatting for short message
            char* p = temp;
            char* prefix = "Child ";
            while (*prefix) *p++ = *prefix++;
            *p++ = '0' + child_index;
            char* middle = ": Short msg ";
            while (*middle) *p++ = *middle++;
            *p++ = '0' + (i % 10);
            *p = '\0';
            msg_len = copy_string(message, temp);
        } else if (i % 3 == 1) {
            char temp[80];
            char* p = temp;
            char* prefix = "Child ";
            while (*prefix) *p++ = *prefix++;
            *p++ = '0' + child_index;
            char* middle = ": Medium length message ";
            while (*middle) *p++ = *middle++;
            *p++ = '0' + (i % 10);
            char* suffix = " with more text";
            while (*suffix) *p++ = *suffix++;
            *p = '\0';
            msg_len = copy_string(message, temp);
        } else {
            char temp[120];
            char* p = temp;
            char* prefix = "Child ";
            while (*prefix) *p++ = *prefix++;
            *p++ = '0' + child_index;
            char* middle = ": Very long message number ";
            while (*middle) *p++ = *middle++;
            *p++ = '0' + (i % 10);
            char* suffix = " with lots of extra text to test variable length handling";
            while (*suffix) *p++ = *suffix++;
            *p = '\0';
            msg_len = copy_string(message, temp);
        }
        
        // Find a place to write the message
        uint64 current_addr = (uint64)buffer;
        uint64 buffer_end = (uint64)buffer + PGSIZE;
        
        while (current_addr < buffer_end) {
            // Check if we have enough space for header + message
            if (current_addr + sizeof(struct msg_header) + msg_len >= buffer_end) {
                // Reached end of buffer
                goto child_exit;
            }
            
            // Try to claim this spot atomically
            uint32* header_ptr = (uint32*)current_addr;
            uint32 old_header = __sync_val_compare_and_swap(header_ptr, 0, 
                (child_index << 16) | (msg_len & 0xFFFF));
            
            if (old_header == 0) {
                // Successfully claimed the spot, now write the message
                char* msg_ptr = (char*)(current_addr + sizeof(struct msg_header));
                
                // Copy message to buffer
                for (int j = 0; j < msg_len; j++) {
                    msg_ptr[j] = message[j];
                }
                
                msg_count++;
                break;
            } else {
                // Spot was taken, move to next potential location
                uint16 occupied_length = old_header & 0xFFFF;
                current_addr += sizeof(struct msg_header) + occupied_length;
                current_addr = align_addr(current_addr);
            }
        }
    }
    
child_exit:
    printf("Child %d wrote %d messages\n", child_index, msg_count);
    exit(0);
}

// Parent process function to read messages
void parent_read_messages(char* buffer) {
    uint64 current_addr = (uint64)buffer;
    uint64 buffer_end = (uint64)buffer + PGSIZE;
    int total_messages = 0;
    
    printf("Parent starting to read messages...\n");
    
    // Continue reading until we've processed the entire buffer
    // or no new messages appear for a while
    int empty_scans = 0;
    int max_empty_scans = 100;
    
    while (empty_scans < max_empty_scans) {
        current_addr = (uint64)buffer;
        int messages_this_scan = 0;
        
        while (current_addr < buffer_end) {
            // Check if we have space for at least a header
            if (current_addr + sizeof(struct msg_header) >= buffer_end) {
                break;
            }
            
            uint32* header_ptr = (uint32*)current_addr;
            uint32 header_val = *header_ptr;
            
            if (header_val == 0) {
                // Empty slot, move to next aligned position
                current_addr += sizeof(struct msg_header);
                current_addr = align_addr(current_addr);
                continue;
            }
            
            // Extract header information
            uint16 child_index = (header_val >> 16) & 0xFFFF;
            uint16 msg_length = header_val & 0xFFFF;
            
            // Check if we have space for the full message
            if (current_addr + sizeof(struct msg_header) + msg_length > buffer_end) {
                break;
            }
            
            // Read and print the message
            char* msg_ptr = (char*)(current_addr + sizeof(struct msg_header));
            printf("Message from child %d (len=%d): ", child_index, msg_length);
            for (int i = 0; i < msg_length; i++) {
                printf("%c", msg_ptr[i]);
            }
            printf("\n");
            
            // Clear the header to mark as processed
            *header_ptr = 0;
            
            total_messages++;
            messages_this_scan++;
            
            // Move to next message location
            current_addr += sizeof(struct msg_header) + msg_length;
            current_addr = align_addr(current_addr);
        }
        
        if (messages_this_scan == 0) {
            empty_scans++;
        } else {
            empty_scans = 0;
        }
    }
    
    printf("Parent finished reading. Total messages processed: %d\n", total_messages);
}

int main(int argc, char *argv[]) {
    printf("Starting multi-process logging test with %d children\n", NUM_CHILDREN);
    
    // Allocate shared buffer in parent
    char* buffer = (char*)sbrk(PGSIZE);
    if (buffer == (char*)-1) {
        printf("Failed to allocate buffer\n");
        exit(1);
    }
    
    // Initialize buffer to zero
    for (int i = 0; i < PGSIZE; i++) {
        buffer[i] = 0;
    }
    
    int parent_pid = getpid();
    
    // Fork child processes
    for (int i = 0; i < NUM_CHILDREN; i++) {
        int pid = fork();
        if (pid == 0) {
            // Child process - map the shared buffer
            uint64 child_buffer = map_shared_pages(parent_pid, (uint64)buffer, PGSIZE);
            if (child_buffer == (uint64)-1) {
                printf("Child %d: Failed to map shared buffer\n", i);
                exit(1);
            }
            
            // Start logging
            child_process(i, (char*)child_buffer);
            
            // Cleanup
            unmap_shared_pages(child_buffer, PGSIZE);
            exit(0);
        } else if (pid < 0) {
            printf("Fork failed for child %d\n", i);
            exit(1);
        }
    }
    
    // Parent process - start reading messages
    parent_read_messages(buffer);
    
    printf("Logging test completed\n");
    exit(0);
}