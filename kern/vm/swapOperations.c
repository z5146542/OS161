#include <swapOperations.h> // for access to the swap operations interface 

/* Swaps out the page present at a particular index of the coremap 
	as indicated by indexToSwap */
void swapout(int indexToSwap) {
	
	// check if the firstSwap flag is uninitiliased and initialize the vnode for the swap file
	if (firstSwapOccur == false) {
		firstSwapOccur = true;
		vfs_open("lhd0raw:", O_RDWR, 0, &swapFile); 	
	}

	// declare variables
	int probeResult;
	struct page_table_entry * pteOfSwap;

	/* Step 1: Remove the translation from the TLB if it exists */
	// probe the TLB for the entry
	probeResult = tlb_probe(coremap[indexToSwap].va, 0);
	if (probeResult >= 0) {
		// invalidate the entry by loading an unmapped address into the TLB
		tlb_write(TLBHI_INVALID(probeResult), TLBLO_INVALID(), i);	
	} else {
		// do nothing
	}

	/* Step 2: Copy the contents of the page to the disk if page is not clean */		
	if (coremap[indexToSwap].state != 2) {
		write_page(indexToSwap);
	}

	/* Step 3: Update the page table entry to indicate that the contents are on disk */
	pteOfSwap = pgdir_walk(coremap[indexToSwap].as, coremap[indexToSwap].va);
	pteOfSwap->pa = 0;
	pteOfSwap->inDisk = true;

	/* Step 4: Evict the page now that its clean */
	// reset the coremap entry
	coremap[indexToSwap].as = NULL;
	coremap[indexToSwap].va = 0;
	coremap[indexToSwap].state = FREE_PAGE;
	coremap[indexToSwap].npages = 0;
	
	// zero the region
	as_zero_region(indexToSwap*PAGE_SIZE, 1);
	
}

/* Swaps in page whose details are denoted by PTE to the index 
	as indicated by indexToSwap */
void swapin(struct page_table_entry * tempPTE, int indexToSwap) {
	
	int swapMapLocation;
	/* Step 1: locate the page on disk using the page table entry and the swapMap */
	swapMapLocation = locate_swap_page(tempPTE->as, tempPTE->va);	

	/* Step 2: Copy the contents from disk to the index designated for the swap in operation */
	read_page(indexToSwap, swapMapLocation);

	/* Step 3: Update the page table entry to indicate that the page is in memory */
	tempPTE->inDisk = false;

}


/* IO operation functions on the swap file 
	write_page - write page contents to a specified region of the swap file
	read_page - read page contents from the swap file to a particular region of the coremap
*/
void write_page(int indexToSwapOut) {
	
	/* Step 1: check if the page contents already exist in the swap file, if so overwrite it */
	struct iovec iov;
	struct uio user;

	int swapMapOffset = locate_swap_page(coremap[indexToSwapOut].as, coremap[indexToSwapOut].va);

	if (swapMapOffset >= 0) {
	
		//Do Nothing	
	
	} else { /* Step 2: if not, find a free swap entry in the swapMap and write to that region */
	
		swapMapOffset = locate_free_entry();
	}

	iov.iov_kbase = PADDR_TO_KVADDR(indexToSwapOut*PAGE_SIZE);
	iov.iov_len = PAGE_SIZE;
	user.uio_iov = &iov;
	user.uio_iovcnt = 1;
	user.uio_offset = swapMapOffset * PAGE_SIZE;
	user.uio_resid = PAGE_SIZE;
	user.uio_rw = UIO_WRITE;
	user.uio_segflg = UIO_SYSSPACE;
	user.uio_space = coremap[indexToSwapOut].as;
	
	int err = VOP_WRITE(swapFile, &user);
	KASSERT(err == 0);
	KASSERT(PAGE_SIZE-user.uio_resid == PAGE_SIZE);

	return;
		
} 

void read_page(int indexToSwapIn, int indexOnMap) {
	
	/* Step 1: Initialise the uio and read from the file into the area of physical memory */
	struct iovec iov;
	struct uio user;

	iov.iov_kbase = PADDR_TO_KVADDR(indexToSwapIn * PAGE_SIZE);
	iov.iov_len = PAGE_SIZE;
	user.uio_iov = &iov;
	user.uio_iovcnt = 1;
	user.uio_offset = indexOnMap * PAGE_SIZE;
	user.uio_resid = PAGE_SIZE;
	user.uio_rw = UIO_READ;
	user.uio_segflg = UIO_SYSSPACE;
	user.uio_space = coremap[indexToSwapIn].as;
	
	int err = VOP_READ(swapFile, &user);
	KASSERT(err == 0);
	KASSERT(PAGE_SIZE-user.uio_resid == PAGE_SIZE);

	return;

}

/* Function to locate a free entry in the swapMap array */
int locate_free_entry() {
	
	/* Step 1: Find an entry that is not pointing to a page and return that 
		or else return -1 to indicate that the swapMap is full  */
	for (int i=0; i < MAX_SWAPPED_PAGES; i++){

		if(swapMap[i] == NULL){
			return i;
		}
	}

	return -1;

}

/* Function to locate the position of the page in the swap file by matching (as, va) as the key */
int locate_swap_page(struct addrspace * as, vaddr_t va) {
	
	/* Step 1: Find the location of the page in the swapMap by matching with as and va 
		or else return -1 to indicate no match found */
	for(int i=0; i < MAX_SWAPPED_PAGES; i++){
		if(swapMap[i]->as == as && swapMap[i]->va == va){
			return i;
		}
	}

	return -1;

}


