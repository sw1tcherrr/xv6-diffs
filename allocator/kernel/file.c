//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];

// global lock is (now) unnecessary

void fileinit(void) {
	// NOP
}

// Allocate a file structure.
struct file *filealloc(void) {
	struct file *f = bd_malloc(sizeof(struct file));
	if (f) {
		initlock(&f->lock, "file_lock");
		acquire(&f->lock);
		f->ref = 1;
		release(&f->lock);
	}

	return f;
}

// Increment ref count for file f.
struct file *filedup(struct file *f) {
	acquire(&f->lock);
	if (f->ref < 1) {
		panic("filedup");
	}
	f->ref++;
	release(&f->lock);
	return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f) {
	acquire(&f->lock); // definitely need to protect ref
	if (f->ref < 1) {
		panic("fileclose");
	}
	if (--f->ref > 0) {
		release(&f->lock);
		return;
	}

	int type = f->type;
	f->type = FD_NONE;
	release(&f->lock);

	if (type == FD_PIPE) {
		pipeclose(f->pipe, f->writable);
	} else if (type == FD_INODE || type == FD_DEVICE) {
		begin_op();
		iput(f->ip);
		end_op();
	}

	bd_free(f);

	// ff doesn't protect like lock
	// it stores the state before invalidation
	// but we don't need the full copy

	// keeping the lock till the end
	// downgrades readability and adds overhead, because:
	// 	begin_op()/end_op() -> sleep() -> sched()
	// 	to run sched() you may hold only one lock
	// 	so need to release the file lock and acquire again inside the transaction

	// let's assume (like in other functions) that file members apart from ref don't need to be protected
	// because we believe that they are changed only when the file in created in sys_open()
	// (it can't be done in parallel with the same file + it's in the locked section)

	// it's ok if someone interrupts us before we get into pipeclose(), iput() or bd_free()
	// and there are necessary locks inside those functions
	// but for additional safety let's show other threads that our type = FD_NONE (and save initial type in variable)
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr) {
	struct proc *p = myproc();
	struct stat st;

	if (f->type == FD_INODE || f->type == FD_DEVICE) {
		ilock(f->ip);
		stati(f->ip, &st);
		iunlock(f->ip);
		if (copyout(p->pagetable, addr, (char *) &st, sizeof(st)) < 0)
			return -1;
		return 0;
	}
	return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64 addr, int n) {
	int r = 0;

	if (f->readable == 0)
		return -1;

	if (f->type == FD_PIPE) {
		r = piperead(f->pipe, addr, n);
	} else if (f->type == FD_DEVICE) {
		if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
			return -1;
		r = devsw[f->major].read(1, addr, n);
	} else if (f->type == FD_INODE) {
		ilock(f->ip);
		if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
			f->off += r;
		iunlock(f->ip);
	} else {
		panic("fileread");
	}

	return r;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n) {
	int r, ret = 0;

	if (f->writable == 0)
		return -1;

	if (f->type == FD_PIPE) {
		ret = pipewrite(f->pipe, addr, n);
	} else if (f->type == FD_DEVICE) {
		if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
			return -1;
		ret = devsw[f->major].write(1, addr, n);
	} else if (f->type == FD_INODE) {
		// write a few blocks at a time to avoid exceeding
		// the maximum log transaction size, including
		// i-node, indirect block, allocation blocks,
		// and 2 blocks of slop for non-aligned writes.
		// this really belongs lower down, since writei()
		// might be writing a device like the console.
		int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
		int i = 0;
		while (i < n) {
			int n1 = n - i;
			if (n1 > max)
				n1 = max;

			begin_op();
			ilock(f->ip);
			if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
				f->off += r;
			iunlock(f->ip);
			end_op();

			if (r != n1) {
				// error from writei
				break;
			}
			i += r;
		}
		ret = (i == n ? n : -1);
	} else {
		panic("filewrite");
	}

	return ret;
}
