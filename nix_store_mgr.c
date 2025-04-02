/*
 * nix_store_mgr.c - QNX Resource Manager for Nix store
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include "nix_store.h"

static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t io_funcs;
static iofunc_attr_t attr;

// Custom open handler for the Nix store
int nix_store_open(resmgr_context_t *ctp, io_open_t *msg, iofunc_attr_t *handle, void *extra) {
    // Implement custom open logic for store paths
    return iofunc_open_default(ctp, msg, handle, extra);
}

// Custom read handler for the Nix store
int nix_store_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb) {
    // Implement custom read logic for store paths
    return iofunc_read_default(ctp, msg, ocb);
}

// Custom write handler to prevent writes to immutable store
int nix_store_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;
    
    // Verify this is a valid write request
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK)
        return status;
    
    // Check extended message type
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE)
        return ENOSYS;
    
    // Set write nbytes to 0 since we're not allowing writes (store is immutable)
    _IO_SET_WRITE_NBYTES(ctp, 0);
    
    // Return read-only filesystem error
    return EROFS;
}

// Initialize the resource manager
int init_resource_manager(void) {
    dispatch_t *dpp;
    dispatch_context_t *ctp;
    
    // Set up the dispatch interface
    dpp = dispatch_create();
    if (dpp == NULL) {
        fprintf(stderr, "Unable to create dispatch interface: %s\n", strerror(errno));
        return -1;
    }
    
    // Initialize the resource manager attributes
    iofunc_attr_init(&attr, S_IFNAM | 0555, NULL, NULL);
    
    // Initialize the functions for handling messages
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, 
                      _RESMGR_IO_NFUNCS, &io_funcs);
                      
    // Override specific functions if needed
    connect_funcs.open = nix_store_open;
    io_funcs.read = nix_store_read;
    io_funcs.write = nix_store_write;
    
    // Attach the resource manager
    resmgr_attach(dpp, NULL, "/dev/nix-store", _FTYPE_ANY, 0, 
                  &connect_funcs, &io_funcs, &attr);
    
    // Start the resource manager
    ctp = dispatch_context_alloc(dpp);
    if (ctp == NULL) {
        fprintf(stderr, "Unable to allocate dispatch context: %s\n", strerror(errno));
        return -1;
    }
    
    // Block and handle messages
    while (1) {
        if ((ctp = dispatch_block(ctp)) == NULL) {
            fprintf(stderr, "Block error: %s\n", strerror(errno));
            return -1;
        }
        
        dispatch_handler(ctp);
    }
    
    // Clean up (never reached in this example)
    dispatch_context_free(ctp);
    
    return 0;
}