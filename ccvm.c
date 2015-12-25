#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

// xxd -r -p tmp
#define DEBUG

#define L_ROOT -4
#define L_END -3
#define D_RDY -2
#define D_BLC -1

#define READY 1
#define BLOCKED 2
#define SLEEPING 3
#define STOPPED 4

struct body_info {
	int locals_size;
	int code_size;
	int start_of_code;
};
typedef struct body_info body_info_t;


struct tasks {
	int task_body;		// we need to know which body corresponds to each task

	struct tasks *nxt;
	struct tasks *prv;

	//local task state variables
	int id;
	int state;
	int reg[16];
	int pc;
	int sem;
	int waket;
	char *local_mem;
};

// globals (used by threads in the future)
// For this implemenation we could just define them in main
// and pass them as arguments to functions
char *globalMem;
char *code;
int cur;
struct tasks *cur_addr;	// used to avoid a search in function list_move_to
						// holds the node address of the current running task

struct tasks *rdy_root;	// root for ready task list
struct tasks *blc_root;	// root for blocked task list
body_info_t *bd_info;

void list_init();
void list_insert(int id, int task_body, int task_arg, int id2);		//insert after id2
int  list_remove(struct tasks *node);				// remove id from dst
int  list_move_to(struct tasks *node, int dst);		// dst = D_RDY/D_BLC = destination
struct tasks *list_locate_blocked(int addr);		// locate blocked task at an addr

void read_bytes(int fd,	char *data, int bytes);
void trace(char *str, int len);		//only for debugging
void load_bin(int fd);



int main(int argc, char *argv[]) {
	int fd_bin;

	if (argc != 2) {
		printf("Invalid number of arguments\nExiting..\n");
		return(0);
	}
	else {
		fd_bin = open(argv[1], O_RDONLY);
		if (fd_bin == -1) {
			if (errno == ENOENT) {
				printf("No such file in current directory.\nExiting..\n");
				return(0);
			}
			else {
				perror("open");
				exit(1);
			}
		}
	}

	load_bin(fd_bin);

	if (close(fd_bin)) {
		perror("close");
		exit(1);
	}


	return(0);
}


void list_init() {
	rdy_root = (struct tasks *)malloc(sizeof(struct tasks));
	if (rdy_root == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	rdy_root->nxt = rdy_root;
	rdy_root->prv = rdy_root;
	rdy_root->id = -1;

	blc_root = (struct tasks *)malloc(sizeof(struct tasks));
	if (blc_root == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	blc_root->nxt = blc_root;
	blc_root->prv = blc_root;
}


// insert a new node to ready_list and initialise some if its fields
void list_insert(int id, int task_body, int task_arg, int id2) {
	struct tasks *curr;
	struct tasks *curr2;

	curr = (struct tasks *)malloc(sizeof(struct tasks));
	if (curr == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	curr->id = id;
	curr->state = READY;
	curr->task_body = task_body;
	curr->pc = bd_info[task_body].start_of_code;

	curr->local_mem = (char *)malloc(bd_info[task_body].locals_size*sizeof(char));
	if(curr->local_mem == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	curr->local_mem[bd_info[task_body].locals_size-1] = task_arg;

	// stop if node with id2 is found or root is reached (id == -1)
	for(curr2=rdy_root->nxt; (curr2->id!=id2)&&(curr2->id!=-1); curr2=curr2->nxt);

	curr->nxt = curr2->nxt;
	curr->prv = curr2;
	curr->nxt->prv = curr;
	curr->prv->nxt = curr;

}

// we can delete a node only if it belongs to rdy_list
int list_remove(struct tasks *node) {
	struct tasks *curr;

	// id's are unique. check if node exists in list
	for(curr=rdy_root->nxt; (curr->id != node->id)&&(curr->id != -1); curr=curr->nxt);
	if (curr->id == -1) {
		printf("list_remove:\nError: Node does not exist in list or has root id\n");
		return(1);
	}

	node->nxt->prv = node->prv;
	node->prv->nxt = node->nxt;
	free(node);

	return(0);
}


// called only when a task must go blocked or unblocked
// move node with id from D_RDY/D_BLC to D_BLC/D_RDY(=dst)
// if dst == D_BLC then node will be added at the end (before root)
// if dst == D_RDY then node will be added before id of current running task(global cur_addr)
int list_move_to(struct tasks *node, int dst) {
	struct tasks *root1;	// root of the list initially holding the node
	struct tasks *root2;	// root of the list where the node will be finally inserted
	struct tasks *curr1;	// used to check if node is valid
	struct tasks *curr2;	// argument node will be inserted after curr2

	if (dst == D_RDY) {
		root1 = blc_root;
		root2 = rdy_root;
		if (node->id == cur_addr->id) {
			printf("list_move_to:\nError: node and curr_addr are the same\n");
			return(1);
		}
		curr2 = cur_addr->prv;
	}
	else if (dst == D_BLC) {
		root1 = rdy_root;
		root2 = blc_root;
		curr2 = root2->prv;
	}
	else {
		printf("list_move_to:\nError: Unknown list reference (dst).\n");
	}

	// id's are unique. check if node exists in list
	// stop if node with id is reached or root is reached (id == -1)
	for(curr1=root1->nxt; (curr1->id!=node->id)&&(curr1->id!=-1); curr1=curr1->nxt);
	if (curr1->id == -1) {
		printf("list_move_to:\nError: Node does not exist in list or has root id\n");
		return(1);
	}

	// detach node from list
	node->nxt->prv = node->prv;
	node->prv->nxt = node->nxt;

	//insert node to new list and position
	node->nxt = curr2->nxt;
	node->prv = curr2;
	node->nxt->prv = node;
	node->prv->nxt = node;


	return(0);
}

// called only when task must be unblocked from a semaphore
// searches list with blocked tasks and returns the one which is blocked at given addr
struct tasks *list_locate_blocked(int addr) {
	struct tasks *curr;

	for (curr=blc_root->nxt; (curr->sem!=addr)&&(curr->id!=-1); curr=curr->nxt);
	if (curr->id == -1) {
		printf("list_locate_blocked:\nError: No node with such sem addr.\n");
		return(NULL);
	}

	return(curr);
}


// used only for debugging (displaying .bin contents)
void trace(char *str, int len) {
#ifdef DEBUG
	int i;

	/*len = (int)strlen(str);*/
	/*printf("len = %d\n", len);*/
	for(i=0; i<len; i++) {
		printf("%02x ", str[i] & 0xff);
	}
	printf("\n");
#endif
}



// reads the specified amount of bytes from the bin file
// from the point where previous read stopped
void read_bytes(int fd, char *data, int bytes) {
	if (read(fd, data, (size_t)bytes) == -1) {
		perror("read");
		exit(1);
	}
	trace(data, bytes);
}



// reads the bin file
void load_bin(int fd) {
	char magic_beg[5] = { 0xde, 0xad, 0xbe, 0xaf, 0x00};
	char magic_bod[5] = { 0xde, 0xad, 0xc0, 0xde, 0x00};
	char magic_tsk[5] = { 0xde, 0xad, 0xba, 0xbe, 0x00};
	char magic_end[5] = { 0xfe, 0xe1, 0xde, 0xad, 0x00};

	char data[5];
	int i;
	int code_index;		// the point from where we should continue writing data to 'code'

	// we could avoid having that many variables but it is prefered that way
	// for better code understanding
	int global_size;
	int num_of_bodies;
	int tot_code_size;
	int num_of_tasks;
	int code_size;
	int locals_size;
	int task_body;
	int task_arg;

	printf("Loading file..\n");

	printf("***\nHeader Section: \n");

	// read MagicBeg
	/*printf("offset = %d\n", (int)lseek(fd, (off_t)0, SEEK_CUR));*/
	read_bytes(fd, data, 4);
	/*printf("offset = %d\n", (int)lseek(fd, (off_t)0, SEEK_CUR));*/
	data[4] = '\0';
	if (!strcmp(data, magic_beg)) { printf("\tMagicBeg: MATCHES\n"); }
	else { printf("\tMagicBeg: MISSMATCH\nTerminating..\n"); exit(1);}


	// read global memory size
	read_bytes(fd, data, 1);
	global_size = (unsigned char)data[0];
	printf("\tGlobalSize: %d\n", global_size);

	// allocate memory for global memory
	globalMem = (char *)malloc(global_size*sizeof(char)+1);
	if (globalMem == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}


	// read number of bodies
	read_bytes(fd, data, 1);
	num_of_bodies = (unsigned char)data[0];
	printf("\tNumOfBodies: %d\n", num_of_bodies);

	// allocate memory for body info struct
	bd_info = (body_info_t *)malloc(num_of_bodies*sizeof(body_info_t));
	if(bd_info == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}


	// read total_code_size
	read_bytes(fd, data, 2);
	tot_code_size = (unsigned char)data[0];
	tot_code_size = (tot_code_size << 8) + (unsigned char)data[1];
	printf("\tTotCodeSize: %d\n", tot_code_size);

	// read number of tasks
	read_bytes(fd, data, 1);
	num_of_tasks = (unsigned char)data[0];
	printf("\tNumOfTasks: %d\n", num_of_tasks);
	printf("***\n\n");

	// initialize global memory
	printf("***\nGlobalInit Section: \n");
	read_bytes(fd, (char *)globalMem, global_size);
	printf("Done.\n(%d bytes)\n", global_size);


	printf("***\n\n");


	code = (char *)malloc(tot_code_size*sizeof(char));
	if(code == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	code_index = 0;
	// store code and info from body sections
	for(i=0; i<num_of_bodies; i++) {

		printf("***\nBody(%d) Section: \n", (i+1));

		//read MagicBody
		read_bytes(fd, data, 4);
		data[4] = '\0';
		if (!strcmp(data, magic_bod)) { printf("\tMagicBody: MATCHES\n"); }
		else { printf("\tMagicBody: MISSMATCH\nTerminating..\n"); exit(1);}

		// read current body's total local memory size
		read_bytes(fd, data, 1);
		locals_size = (unsigned char)data[0];
		printf("\tLocalsSize: %d\n", locals_size);

		// read body's code size
		read_bytes(fd, data, 1);
		code_size = (unsigned char)data[0];
		printf("\tCodeSize: %d\n", code_size);


		// append variable "code" with new body's code
		bd_info[i].locals_size = locals_size;
		bd_info[i].code_size = code_size;
		bd_info[i].start_of_code = code_index;
		read_bytes(fd, (char *)&code[code_index], code_size);

		code_index += code_size;
		printf("---\nlocals_size = %d\n", bd_info[i].locals_size);
		printf("code_size = %d\n", bd_info[i].code_size);
		printf("start_of_code = %d\n", bd_info[i].start_of_code);
		trace((char *)code, code_index);
		printf("***\n\n");
	}
	/*printf("(Total bodies: %d )\n", i);*/


	for(i=0; i<num_of_tasks; i++) {

		printf("***\nTask(%d) Section: \n", (i+1));

		// read MagicTask
		read_bytes(fd, data, 4);
		data[4] = '\0';
		if (!strcmp(data, magic_tsk)) { printf("\tMagicTask: MATCHES\n"); }
		else { printf("\tMagicTask: MISSMATCH\nTerminating..\n"); exit(1);}

		//TODO: PASS task_body and task_arg TO A NEW LIST NODE

		// read task body number
		read_bytes(fd, data, 1);
		task_body = (unsigned char)data[0];
		printf("\tTaskBody: %d\n", task_body);

		// read task arg and store it to the end of local memory
		read_bytes(fd, data, 1);
		task_arg = (unsigned char)data[0];
		printf("\tTaskArg: %d\n", task_arg);

		printf("***\n\n");

	}
}