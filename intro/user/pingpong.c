#include "kernel/types.h"
#include "user/user.h"

#define ERR_WRAPPER(res, msg, label) \
	if ((res) < 0) { \
		fprintf(2, "%d: %s", getpid(), (msg));               \
		goto label;                                          \
	}

#define PIPE(fd) 		ERR_WRAPPER(pipe((fd)), "Can't pipe", err)
#define WRITE(fd, buf, n) 	ERR_WRAPPER(write((fd), (buf), (n)), "Can't write", err)
#define READ(fd, buf, n)	ERR_WRAPPER(read((fd), (buf), (n)), "Can't read", err)


static int c2p[2];
static int p2c[2];

#define FROM_PARENT p2c[0]
#define TO_PARENT   c2p[1]
#define FROM_CHILD  c2p[0]
#define TO_CHILD    p2c[1]

#define PARENT_MSG "ping"
#define CHILD_MSG  "pong"

#define MAX_LEN 4
static char buf[MAX_LEN];

void close_child() {
	close(TO_PARENT);
	close(FROM_PARENT);
}

void close_parent() {
	close(TO_CHILD);
	close(FROM_CHILD);
}

int main() {
	PIPE(c2p)
	PIPE(p2c)

	int parent_len = strlen(PARENT_MSG);
	int child_len = strlen(CHILD_MSG);

	int pid = fork();
	ERR_WRAPPER(pid, "Can't fork", err);

	if (pid != 0) {
		close_child();

		WRITE(TO_CHILD, PARENT_MSG, parent_len)

		READ(FROM_CHILD, buf, child_len)
		printf("%d: got %s\n", getpid(), buf);
	} else {
		close_parent();

		READ(FROM_PARENT, buf, parent_len)
		printf("%d: got %s\n", getpid(), buf);

		WRITE(TO_PARENT, CHILD_MSG, child_len)
	}

	exit(0);

// there could be another label for clearing resources
// but pipe file descriptors are closed by exit()
err:
	exit(1);
}
