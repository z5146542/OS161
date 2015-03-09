#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>

#include <kern/execv.h>
#include <copyinout.h>
#include <syscall.h>

#define BUFFER_SIZE 255

int sys_execv(const_userptr_t progname, userptr_t args){
	
	
	/* Step 1: get the program name from the user */
	
	char progNameFromUser[BUFFER_SIZE];
	int result = 0;
	size_t actual;
	if ((result = copyinstr(progname, progNameFromUser, BUFFER_SIZE, &actual)) != 0){
		return result;
	}

	/* Step 2: get the number of arguments from userspace */
	int numArgs = 0;
	int random;
	
	
	int i = 0;
	while (*(char **)(args+i) != NULL) {
		copyin(args+i, &random, sizeof(int));
		numArgs++;
		i += 4;
	}
	
	numArgs--;	
	char * commands[numArgs];
	int pointersToGet[numArgs];
	int j = 4;
	for (int i=0; i < numArgs; i++) {
		commands[i] = kmalloc(100*sizeof(char));
		copyin(args + (j*i), &pointersToGet[i], sizeof(int));
		copyinstr((userptr_t)pointersToGet[i] , commands[i], 100, &actual); 
	}

	void * startPoint;
	int numBytes = 0;
	int len;
	int padding = 0;
	/* Creating (numArgs+1) contigous block of pointers of 4 bytes each */
	char nullPadding[3] = "\0\0\0";
	startPoint = kmalloc(sizeof(int *) * (numArgs+1));
	numBytes += (sizeof(int *) * (numArgs+1));
	startPoint += numBytes;
	int intSize = 4;

	for (i=0;i<numArgs; i++){
		*(int *)((startPoint-numBytes) + (i*intSize))  = numBytes;
		len = strlen(commands[i]) +1;
		memcpy(startPoint,commands[i],len);
		numBytes += len;
		startPoint += len;
		padding = 4 - ((int)startPoint % 4);
		if (padding != 4) {
			memcpy(startPoint, nullPadding, padding);
			startPoint += padding;
			numBytes += padding;
		}  		
	}

	startPoint -= numBytes;


	/* Destroy the current address-space */
	as_destroy(curthread->t_addrspace);
	curthread->t_addrspace = NULL;

	/* Rest of the functionality is similar to runprogam */
	struct vnode *v;
        vaddr_t entrypoint, stackptr;

        /* Open the file. */
        result = vfs_open(progNameFromUser, O_RDONLY, 0, &v);
        if (result) {
                return result;
        }

        /* We should be a new thread. */
        KASSERT(curthread->t_addrspace == NULL);

        /* Create a new address space. */
        curthread->t_addrspace = as_create();
        if (curthread->t_addrspace==NULL) {
                vfs_close(v);
                return ENOMEM;
        }

        /* Activate it. */
        as_activate(curthread->t_addrspace);

        /* Load the executable. */
	        result = load_elf(v, &entrypoint);
        if (result) {
                /* thread_exit destroys curthread->t_addrspace */
                vfs_close(v);
                return result;
        }

        /* Done with the file now. */
        vfs_close(v);

        /* Define the user stack in the address space */
        result = as_define_stack(curthread->t_addrspace, &stackptr);
        if (result) {
                /* thread_exit destroys curthread->t_addrspace */
                return result;
        }
	
	int stackStart;
	stackStart = stackptr - numBytes;
	
	/* Update the addresses that need to be copied in the user stack*/
	for (i = 0; i < numArgs; i ++){

		*(int *)startPoint = stackStart + *(int *)startPoint;
		startPoint += 4;
	}	                             

	*(int *)startPoint = (int)NULL;
	startPoint -= (numArgs*4);

	copyout(startPoint, (userptr_t)stackStart, numBytes);

	enter_new_process(numArgs, (userptr_t)stackStart, stackptr, entrypoint);
	
	return 0; 
}