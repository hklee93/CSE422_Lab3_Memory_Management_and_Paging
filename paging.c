/*
* Questions:
* 1. How to disable page fault handler for prepaging
* 2. How does the demand_paging module parameter get used
* 3. Difference between two different methods for prepaging
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>

#include <paging.h>

static atomic_t alloc_cnt;
static atomic_t free_cnt;

static unsigned int demand_paging = 1;
module_param(demand_paging, uint, 0644);

typedef struct vma_tracker_t{
    unsigned int nr_pages;
    unsigned int * page_indices;
    atomic_t refcnt;
} vma_tracker_t;

static void
paging_vma_open(struct vm_area_struct * vma)
{
	vma_tracker_t *vma_tracker;
    printk(KERN_INFO "paging_vma_open invoked\n");

	vma_tracker = vma->vm_private_data;
	atomic_inc(&(vma_tracker->refcnt));
}

static void
paging_vma_close(struct vm_area_struct * vma)
{
	int i;
	vma_tracker_t *vma_tracker;
	struct page *my_page;
    printk(KERN_INFO "paging_vma_close invoked\n");

	vma_tracker = vma->vm_private_data;
	atomic_dec(&(vma_tracker->refcnt));

	if(atomic_read(&(vma_tracker->refcnt)) == 0){

		for(i=0; i<vma_tracker->nr_pages; i++){

			if(vma_tracker->page_indices[i] != 0){
				my_page = pfn_to_page(vma_tracker->page_indices[i]);
				__free_page(my_page);
				atomic_inc(&free_cnt);
			}

		}

		kfree(vma_tracker->page_indices);
		kfree(vma_tracker);
	}

}

static int paging_vma_fault(struct vm_area_struct * vma,
                 struct vm_fault       * vmf)
{
	unsigned int page_offset;
	struct page *my_page;
	int update_ret;
	vma_tracker_t *vma_tracker;
	unsigned long pfn;

    // printk(KERN_ERR "The VMA covers the VA segment [0x%lx, 0x%lx), and the fault was at VA 0x%lx\n",
    //     vma->vm_start, vma->vm_end, (unsigned long)vmf->virtual_address
    // );

	page_offset = (unsigned long)(vmf->virtual_address - vma->vm_start) / PAGE_SIZE;

    vma_tracker = vma->vm_private_data;

	my_page = alloc_page(GFP_KERNEL);

	if(my_page == NULL){
		return VM_FAULT_OOM;
	}

	pfn = page_to_pfn(my_page);
	update_ret = remap_pfn_range(vma, PAGE_ALIGN((unsigned long)vmf->virtual_address), pfn, PAGE_SIZE, vma->vm_page_prot);

	if(update_ret == 0){
		vma_tracker->page_indices[page_offset] = (unsigned int)pfn;
	}
	else{
		return VM_FAULT_SIGBUS;
	}

	atomic_inc(&alloc_cnt);
    return VM_FAULT_NOPAGE;
}

static struct vm_operations_struct
paging_vma_ops =
{
    .open = paging_vma_open,
    .close = paging_vma_close,
    .fault = paging_vma_fault,
};


/* vma is the new virtual address segment for the process */
static int
paging_mmap(struct file           * filp,
            struct vm_area_struct * vma)
{
	int i;
	vma_tracker_t *vma_tracker;

	struct page *my_page;
	unsigned long pfn;
	int update_ret;

    /* prevent Linux from mucking with our VMA (expanding it, merging it
     * with other VMAs, etc.
     */
    vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE | VM_DONTDUMP | VM_PFNMAP;

    /* setup the vma->vm_ops, so we can catch page faults on this vma */
    vma->vm_ops = &paging_vma_ops;

	vma_tracker = kmalloc(sizeof(vma_tracker_t), GFP_KERNEL);

	atomic_set(&(vma_tracker->refcnt), 1);
	vma_tracker->nr_pages = (vma->vm_end - vma->vm_start) / PAGE_SIZE + (((vma->vm_end - vma->vm_start) % PAGE_SIZE) == 0 ? 0:1);
	vma_tracker->page_indices = kmalloc(sizeof(unsigned int) * vma_tracker->nr_pages, GFP_KERNEL);

	for(i=0; i<vma_tracker->nr_pages; i++){
		vma_tracker->page_indices[i] = 0;
	}

	vma->vm_private_data = vma_tracker;

	/* implementing prepaging */
	if(demand_paging == 0){

		for(i=0; i<vma_tracker->nr_pages; i++){
			my_page = alloc_page(GFP_KERNEL);

			if(my_page == NULL){
				return -ENOMEM;
			}

			pfn = page_to_pfn(my_page);
			update_ret = remap_pfn_range(vma, vma->vm_start + i*PAGE_SIZE, pfn, PAGE_SIZE, vma->vm_page_prot);

			if(update_ret == 0){
				vma_tracker->page_indices[i] = (unsigned int)pfn;
			}
			else{
				return -EFAULT;
			}

			atomic_inc(&alloc_cnt);
		}
	}

    return 0;
}

static struct file_operations
dev_ops =
{
    .mmap = paging_mmap,
};

static struct miscdevice
dev_handle =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = PAGING_MODULE_NAME,
    .fops = &dev_ops,
};

/*** Kernel module initialization and teardown ***/
static int
paging_init(void)
{
    int status;

	atomic_set(&alloc_cnt, 0);
	atomic_set(&free_cnt, 0);

    /* Create a character device to communicate with user-space via file I/O operations */
    status = misc_register(&dev_handle);
    if (status != 0)
    {
        printk(KERN_ERR "Failed to register misc. device for module\n");
        return status;
    }

    printk(KERN_INFO "Loaded paging module\n");

    return 0;
}

static void
paging_exit(void)
{
    /* Deregister our device file */
    misc_deregister(&dev_handle);

    printk(KERN_INFO "Unloaded paging module\n");
	printk("alloc count: %d free count: %d\n", atomic_read(&alloc_cnt), atomic_read(&free_cnt));
}

module_init(paging_init);
module_exit(paging_exit);

/* Misc module info */
MODULE_LICENSE("GPL");
