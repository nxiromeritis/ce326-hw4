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

#define LLoad		1
#define LLoadi	 	2
#define GLoad		3
#define GLoadi		4
#define LStore		5
#define LStorei		6
#define GStore		7
#define GStorei		8
#define Set			9
#define Add			10
#define Sub			11
#define Mul			12
#define Div			13
#define Mod			14
#define Brgz		15
#define Brgez		16
#define Brlz		17
#define Brlez		18
#define Brez		19
#define Bra			20
#define Down		21
#define Up			22
#define Yield		23
#define Sleep		24
#define Print		25
#define Exit		26

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

struct tasks *task_info;

// list functions
void list_init();
void list_insert(int id, int task_body, int task_arg);		//insert after root
int  list_delete(struct tasks *node);				// remove id from dst
int  list_move_to(struct tasks *node, int dst);		// dst = D_RDY/D_BLC = destination
struct tasks *list_locate_blocked(int addr);		// locate blocked task at an addr
void print_lists();

void print_node();

// .bin related functions
void read_bytes(int fd,	char *data, int bytes);
void trace(char *str, int len);		//only for debugging
void load_bin(int fd);

//
void run_task (int t){
	char command[9], byte1[9], byte2[9];
	int bd = task_info[t].task_body;
	int n = bd_info[bd].code_size + bd_info[bd].start_of_code;
	int c, b1, b2;
	int i;
	
	i = bd_info[bd].start_of_code;
	do{
		strncat(command, &code[i],8);
		strncat(byte1, &code[i+8],8);
		strncat(byte2, &code[i+16],8);
		c = (int) strtol(command, NULL, 2);
		b1 = (int) strtol(byte1, NULL, 2);
		b2 = (int) strtol(byte2, NULL, 2);
		
		
		switch (c){
			case LLoad:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].local_mem[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case LLoadi:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].local_mem[b2 + task_info[t].reg[0]];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case GLoad:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = globalMem[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case GLoadi:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = globalMem[b2 + task_info[t].reg[0]];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case LStore:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].local_mem[b1] = task_info[t].reg[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case LStorei:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].local_mem[b1] = task_info[t].reg[b2 + task_info[t].reg[0]];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case GStore:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				//strncpy(globalMem[b1],task_info[t].reg[b2],8);
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case GStorei:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				globalMem[b1] = task_info[t].reg[b2 + task_info[t].reg[0]];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Set:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = b2;
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Add:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].reg[b1] + task_info[t].reg[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Sub:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].reg[b1] - task_info[t].reg[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Mul:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].reg[b1] * task_info[t].reg[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Div:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].reg[b1] / task_info[t].reg[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Mod:{
				printf("\tBefore: %d",task_info[t].reg[b1]);
				task_info[t].reg[b1] = task_info[t].reg[b1] % task_info[t].reg[b2];
				printf("\tAfter: %d",task_info[t].reg[b1]);
				break;
			}
			case Brgz:{
				if (task_info[t].reg[b1] == 0){
					i = i + b2;
				}
				continue;
			}
			case Brgez:{
				if (task_info[t].reg[b1] > 0){
					i = i + b2;
				}
				continue;
			}
			case Brlz:{
				if (task_info[t].reg[b1] >= 0){
					i = i + b2;
				}
				continue;
			}
			case Brlez:{
				if (task_info[t].reg[b1] < 0){
					i = i + b2;
				}
				continue;
			}
			case Brez:{
				if (task_info[t].reg[b1] <= 0){
					i = i + b2;
				}
				continue;
			}
			case Bra:{
				i = i + b2;
				continue;
			}
			case Down:{}
			case Up:{}
			case Yield:{}
			case Sleep:{
				sleep(b1);
				break;
			}
			case Print:{}
			case Exit:{}
			default:{
				printf("Invalid command\nExiting..\n");
				exit(0);
			}
		}
		i = i +24;
	}while ( i < n);
	
}
//

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
	blc_root->id = -1;
}


// instert to ready(state) list and initialize node
void list_insert(int id, int task_body, int task_arg) {
	struct tasks *curr;

	curr = (struct tasks *)malloc(sizeof(struct tasks));
	if (curr == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}

	// initialize node fields
	curr->id = id;
	curr->state = READY;
	curr->task_body = task_body;
	curr->pc = bd_info[task_body-1].start_of_code;

	curr->local_mem = (char *)malloc(bd_info[task_body].locals_size*sizeof(char));
	if(curr->local_mem == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	// put task argument to last local memory segment
	curr->local_mem[bd_info[task_body].locals_size-1] = task_arg;

	curr->sem = -1;
	curr->waket = -1;

	curr->nxt = rdy_root->nxt;
	curr->prv = rdy_root;
	curr->nxt->prv = curr;
	curr->prv->nxt = curr;

	print_node(curr);
}



// we can delete a node only if it belongs to rdy_list
int list_delete(struct tasks *node) {
	struct tasks *curr;

	// id's are unique. check if node exists in rdy_list
	for(curr=rdy_root->nxt; (curr->id != node->id)&&(curr->id != -1); curr=curr->nxt);
	if (curr->id == -1) {
		printf("list_remove:\nError: Node does not exist in list or has root id\n");
		return(1);
	}

	if (curr->state != STOPPED) {
		printf("list_delete: Task terminated unexpectedly (state != STOPPED)..\n");
	}

	node->nxt->prv = node->prv;
	node->prv->nxt = node->nxt;
	free(node->local_mem);
	free(node);

	return(0);
}


// called only when a task must go blocked or ready
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
		// in this case we want one task (node) to go from blocked to ready
		// this cant be cur_addr because this is the one that called UP
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

void print_lists() {
	struct tasks *curr;

	printf("NonBlocked:");
	for (curr=rdy_root->nxt; (curr->id!=-1); curr=curr->nxt){
		printf(" %d->", curr->id);
	}
	printf("\n");

	printf("\nBlocked:");
	for (curr=blc_root->nxt; (curr->id!=-1); curr=curr->nxt) {
		printf(" %d->", curr->id);
	}
	printf("\n");
}

void print_node(struct tasks *node) {
#ifdef DEBUG
	printf("\nNew Task:\n");
	printf("ID: \t%d\n", node->id);
	printf("STATE: \t%d\n", node->state);
	printf("PC: \t%d\n", node->pc);
	printf("SEM: \t%d\n", node->sem);
	printf("WAKET: \t%d\n", node->waket);
	printf("ARG: \t%d\n", node->local_mem[bd_info[node->task_body].locals_size-1]);
	printf("TSKBOD: \t%d\n", node->task_body);
	printf("\n");
#endif
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
	globalMem = (char *)malloc(global_size*sizeof(char));
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

	
	task_info = (struct tasks *)malloc(num_of_tasks*sizeof(struct tasks));
	if(task_info == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}

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

		// store usefull information for later
		bd_info[i].locals_size = locals_size;
		bd_info[i].code_size = code_size;
		bd_info[i].start_of_code = code_index;
		read_bytes(fd, (char *)&code[code_index], code_size);

		// append variable "code" with new body's code
		code_index += code_size;
		printf("---\nlocals_size = %d\n", bd_info[i].locals_size);
		printf("code_size = %d\n", bd_info[i].code_size);
		printf("start_of_code = %d\n", bd_info[i].start_of_code);
		trace((char *)code, code_index);
		printf("***\n\n");
	}
	/*printf("(Total bodies: %d )\n", i);*/

	list_init();

	for(i=0; i<num_of_tasks; i++) {

		printf("***\nTask(%d) Section: \n", (i+1));

		// read MagicTask
		read_bytes(fd, data, 4);
		data[4] = '\0';
		if (!strcmp(data, magic_tsk)) { printf("\tMagicTask: MATCHES\n"); }
		else { printf("\tMagicTask: MISSMATCH\nTerminating..\n"); exit(1);}
		
		// read task body number
		read_bytes(fd, data, 1);
		task_body = (unsigned char)data[0];
		printf("\tTaskBody: %d\n", task_body);

		// read task arg and store it to the end of local memory
		read_bytes(fd, data, 1);
		task_arg = (unsigned char)data[0];
		printf("\tTaskArg: %d\n", task_arg);
		
		task_info[i].task_body = task_body;
		task_info[i].id = i;
		

		list_insert(i, task_body, task_arg);
		printf("***\n\n");
		print_lists();

		run_task(i);
	}

	// read MagicEnd
	read_bytes(fd, data, 4);
	data[4] = '\0';
	if (!strcmp(data, magic_end)) { printf("\tMagicEnd: MATCHES\n"); }
	else { printf("\tMagicEnd: MASSMATCH\nTerminating..\n"); exit(1);}
}
