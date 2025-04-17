/*
 * nix_store_mgr.c - Minimal QNX Resource Manager for Nix store
 * Implements a read-only file to mimic/proof of concept version for the daemon interface.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <errno.h>
 #include <sys/iofunc.h>
 #include <sys/dispatch.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <dirent.h>
 #include <limits.h>
 #include <unistd.h>
 
 #define MIN(a, b) (((a) < (b)) ? (a) : (b))
 
 static resmgr_connect_funcs_t connect_funcs;
 static resmgr_io_funcs_t io_funcs;
 static iofunc_attr_t attr;

 void log_message(const char *message) {
     FILE *log = fopen("/data/log/nix_daemon.log", "a");
     if (log) {
         fprintf(log, "%s\n", message);
         fclose(log);
     }
 }
 
 int nix_store_open(resmgr_context_t *ctp, io_open_t *msg, iofunc_attr_t *handle, void *extra) {
    char log_buffer[512];
    
    // request to the root of our resource manager (empty path)
    // This is what happens "cat /dev/nix-store"
    if (msg->connect.path[0] == '\0') {
        log_message("Open request for root of resource manager");
        return iofunc_open_default(ctp, msg, handle, extra);
    }
    
    // If there is a specific path, validate it
    char resolved_path[PATH_MAX];
    snprintf(resolved_path, sizeof(resolved_path), "/data/nix/store/%s", msg->connect.path);
    
    // Check if the path exists
    struct stat st;
    if (stat(resolved_path, &st) == -1) {
        snprintf(log_buffer, sizeof(log_buffer), "Failed to open path: %.300s, error: %.100s", 
                resolved_path, strerror(errno));
        log_message(log_buffer);
        return errno;
    }
    
    snprintf(log_buffer, sizeof(log_buffer), "Opened: %.400s", resolved_path);
    log_message(log_buffer);
    
    return iofunc_open_default(ctp, msg, handle, extra);
}

 
 // Static buffer to hold our directory listing - must be global
static char store_buffer[8192];
static size_t store_buffer_size = 0;
static int buffer_initialized = 0;

// Read handler: returns the contents of the store directory as a plain text listing
int nix_store_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb) {
    log_message("Read request received");
    
    // Generate the content on first read or if not initialized
    if (!buffer_initialized) {
        DIR *dir = opendir("/data/nix/store");
        if (!dir) {
            log_message("Failed to open store directory");
            return errno;
        }
        
        // Clear and populate the buffer
        memset(store_buffer, 0, sizeof(store_buffer));
        size_t off = 0;
        
        // Add a header
        off += snprintf(store_buffer + off, sizeof(store_buffer) - off, "Nix Store Contents:\n");
        off += snprintf(store_buffer + off, sizeof(store_buffer) - off, "------------------\n");
        
        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL && off < sizeof(store_buffer) - 128) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            
            off += snprintf(store_buffer + off, sizeof(store_buffer) - off, "%s\n", entry->d_name);
            count++;
        }
        
        closedir(dir);
        
        // Log the number of entries found
        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf), "Found %d entries in store", count);
        log_message(log_buf);
        
        // If no entries, add a message
        if (count == 0) {
            off += snprintf(store_buffer + off, sizeof(store_buffer) - off, "(No items in store)\n");
        }
        
        // Update the file size
        store_buffer_size = off;
        attr.nbytes = off;
        buffer_initialized = 1;
        
        // Debug: Log the buffer contents
        log_message("Buffer contents:");
        log_message(store_buffer);
    }
    
    // Handle EOF condition
    if (ocb->offset >= store_buffer_size) {
        log_message("EOF reached");
        return 0;
    }
    
    // Calculate bytes to return
    int bytes_available = store_buffer_size - ocb->offset;
    int nbytes = MIN(msg->i.nbytes, bytes_available);
    
    // Set up IO vector for reply
    SETIOV(ctp->iov, store_buffer + ocb->offset, nbytes);
    ocb->offset += nbytes;
    
    // Log what we're returning
    char log_buffer[128];
    snprintf(log_buffer, sizeof(log_buffer), "Read %d bytes, offset now %lld", 
             nbytes, (long long)ocb->offset);
    log_message(log_buffer);
    
    // Return the number of bytes read
    _IO_SET_READ_NBYTES(ctp, nbytes);
    
    return _RESMGR_NPARTS(1);
}

 // Write handler: The store is immutable, so we always return an error.
 int nix_store_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
     return EROFS; // Read-only file system error
 }
 
 // Initialization of the resource manager and the main event loop
 int init_resource_manager(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0)
        exit(0);  // Parent process exits

    setsid();
    chdir("/");
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    dispatch_t *dpp;
    dispatch_context_t *ctp;
    
    // Create dispatch structure for managing I/O requests
    dpp = dispatch_create();
    if (!dpp) {
        log_message("Failed to create dispatch structure");
        return -1;
    }
    
    // Initialize resource attributes as a regular file with read-only permissions
    iofunc_attr_init(&attr, S_IFREG | 0444, 0, 0);
    attr.nbytes = 0; // Will be updated during read operations
    
    // Initialize connection and I/O function tables
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
    
    // Assign our custom functions
    connect_funcs.open = nix_store_open;
    io_funcs.read = nix_store_read;
    io_funcs.write = nix_store_write;
    
    // Attach our resource manager at "/dev/nix-store"
    if (resmgr_attach(dpp, NULL, "/dev/nix-store", _FTYPE_ANY, 0, 
                      &connect_funcs, &io_funcs, &attr) == -1) {
        log_message("Failed to attach resource manager to /dev/nix-store");
        return -1;
    }
    
    log_message("Nix store daemon started successfully");
    
    // For a proof of concept, we'll skip daemonization to make debugging easier
    // Allocate initial dispatch context and enter the event loop
    ctp = dispatch_context_alloc(dpp);
    if (!ctp) {
        log_message("Failed to allocate dispatch context");
        return -1;
    }
    
    // Main event loop
    while (1) {
        ctp = dispatch_block(ctp);
        if (!ctp) {
            log_message("Dispatch block error");
            break;
        }
        dispatch_handler(ctp);
    }
    
    return 0;
}