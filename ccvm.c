#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>


// xxd -r -p tmp
#define DEBUG2

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
	int min_pc;			// min/max pc values are needed to check if a task by..
	int max_pc;			// ..mistake accesses another body

	struct tasks *nxt;
	struct tasks *prv;

	//local task state variables
	int id;
	int state;
	char reg[16];
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
struct tasks *cur;		// used to avoid a search in function list_move_to
						// holds the node address of the current running task

struct tasks *rdy_root;	// root for ready task list
struct tasks *blc_root;	// root for blocked task list
body_info_t *bd_info;

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
int load_bin(int fd);
void run_bin(int num_of_tasks);


int main(int argc, char *argv[]) {
	int fd_bin;
	int num_of_tasks;
	int i;

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

	num_of_tasks = load_bin(fd_bin);

	printf("Paused..\n");
	scanf("%d", &i);

	run_bin(num_of_tasks);

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


// insert to ready(state) list and initialize node
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
	curr->min_pc = bd_info[task_body-1].start_of_code;
	curr->max_pc = curr->min_pc + bd_info[task_body-1].code_size - 1;
	curr->pc = bd_info[task_body-1].start_of_code;

	curr->local_mem = (char *)malloc(bd_info[task_body-1].locals_size*sizeof(char));
	if(curr->local_mem == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	// put task argument to last local memory segment
	curr->local_mem[bd_info[task_body-1].locals_size-1] = task_arg;

	curr->sem = -1;
	curr->waket = -1;

	curr->nxt = rdy_root->nxt;
	curr->prv = rdy_root;
	curr->nxt->prv = curr;
	curr->prv->nxt = curr;

	print_node(curr);
	print_lists();
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
// if dst == D_RDY then node will be added before id of current running task(global cur)
int list_move_to(struct tasks *node, int dst) {
	struct tasks *root1;	// root of the list initially holding the node
	struct tasks *root2;	// root of the list where the node will be finally inserted
	struct tasks *curr1;	// used to check if node is valid
	struct tasks *curr2;	// argument node will be inserted after curr2

	if (dst == D_RDY) {
		root1 = blc_root;
		root2 = rdy_root;
		// in this case we want one task (node) to go from blocked to ready
		// this cant be cur because this is the one that called UP
		if (node->id == cur->id) {
			printf("list_move_to:\nError: node and curr_addr are the same\n");
			return(1);
		}
		curr2 = cur->prv;
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
#ifdef DEBUG
	struct tasks *curr;

	printf("NonBlocked:");
	for (curr=rdy_root->nxt; (curr->id!=-1); curr=curr->nxt){
		printf(" %d->", curr->id);
	}
	printf("\n");

	printf("Blocked:");
	for (curr=blc_root->nxt; (curr->id!=-1); curr=curr->nxt) {
		printf(" %d->", curr->id);
	}
	printf("\n");
#endif
}

void print_node(struct tasks *node) {
#ifdef DEBUG
	printf("\nNew Task:\n");
	printf("ID: \t%d\n", node->id);
	printf("STATE: \t%d\n", node->state);
	printf("PC: \t%d\n", node->pc);
	printf("SEM: \t%d\n", node->sem);
	printf("WAKET: \t%d\n", node->waket);
	printf("ARG: \t%d\n", node->local_mem[bd_info[node->task_body-1].locals_size-1]);
	printf("TSKBD: \t%d\n", node->task_body);
	printf("MINPC: \t%d\n", node->min_pc);
	printf("MAXPC: \t%d\n", node->max_pc);
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
int load_bin(int fd) {
	char magic_beg[5] = { 0xde, 0xad, 0xbe, 0xaf, 0x00};
	char magic_bod[5] = { 0xde, 0xad, 0xc0, 0xde, 0x00};
	char magic_tsk[5] = { 0xde, 0xad, 0xba, 0xbe, 0x00};
	char magic_end[5] = { 0xfe, 0xe1, 0xde, 0xad, 0x00};

	char data[5];
	int i;
	unsigned int code_index;	// the point from where we should continue writing data to 'code'

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

	printf("\nHeader Section: \n");

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
	printf("\tNumOfTasks: %d\n\n", num_of_tasks);

	// initialize global memory
	printf("GlobalInit Section: \n");
	read_bytes(fd, (char *)globalMem, global_size);
	printf("(Stored %d bytes)\n\n", global_size);



	code = (char *)malloc(tot_code_size*sizeof(char));
	if(code == NULL) {
		printf("Memory allocation problems.\nExiting..\n");
		exit(1);
	}
	code_index = 0;
	// store code and info from body sections
	for(i=0; i<num_of_bodies; i++) {

		printf("Body(%d) Section: \n", (i+1));

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
		printf("\n");
	}
	/*printf("(Total bodies: %d )\n", i);*/

	list_init();

	for(i=0; i<num_of_tasks; i++) {

		printf("Task(%d) Section: \n", (i+1));

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

		list_insert(i, task_body, task_arg);
		printf("\n");
	}

	// read MagicEnd
	printf("Footer Section: \n");
	read_bytes(fd, data, 4);
	data[4] = '\0';
	if (!strcmp(data, magic_end)) { printf("\tMagicEnd: MATCHES\n"); }
	else { printf("\tMagicEnd: MASSMATCH\nTerminating..\n"); exit(1);}

	return(num_of_tasks);
}

void run_bin(int num_of_tasks) {
	int tasks_stopped = 0;
	int i;
	int inum;
	unsigned char ibyte1;
	unsigned char ibyte2;
	unsigned char ibyte3;
	struct tasks *blc_node;
	struct tasks *cur_copy;

	printf("\nStart of execution:\n");

	cur_copy = NULL;
	cur=rdy_root;
	srand(time(NULL));

	while(1) {

		cur=cur->nxt;

#ifdef DEBUG
		print_lists();
		printf("dbg: switch to id: %d\n", cur->id);
		/*printf("stopped(%d) vs numoftasks(%d)\n", tasks_stopped, num_of_tasks);*/
#endif


		if (cur_copy != NULL) {
			if (cur_copy->state == STOPPED) {list_delete(cur_copy);}
			if (cur_copy->state == BLOCKED) {list_move_to(cur_copy, D_BLC);}
			cur_copy = NULL;
		}

		if (cur->id==-1) {cur=cur->nxt;}

		if (cur==cur->nxt) {
			if (tasks_stopped!=num_of_tasks) {
				printf("Error: DEADLOCK\n");
				exit(1);
			}
			else {
				printf("All tasks STOPPED (successfuly).\nEnd of program.\n");
				return;
			}
		}

		if ((cur->state==SLEEPING)&&(time(NULL) < cur->waket)) { continue;}
		if ((cur->state==SLEEPING)&&(time(NULL) >= cur->waket)) {cur->waket=-1;}

		i = 0;
		inum = rand()%2+1;
		while (i<inum) {
			ibyte1 = code[cur->pc];
			ibyte2 = code[cur->pc+1];
			ibyte3 = code[cur->pc+2];

#ifdef DEBUG
			printf("dbg%d: %02x %02x %02x (pc:%d)\n\n",cur->id,ibyte1&0xff,ibyte2&0xff,ibyte3&0xff, cur->pc/3);
#endif

			/*sleep(2);*/
			switch (ibyte1) {
				case 0x01: {   // LLOAD
							   cur->reg[ibyte2] = cur->local_mem[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x02: {   // LLOADi
							   cur->reg[ibyte2] = cur->local_mem[ibyte3 + cur->reg[0]];
							   cur->pc += 3;
							   break;
						   }
				case 0x03: {   // GLOAD
							   cur->reg[ibyte2] = globalMem[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x04: {   // GLOADi
							   cur->reg[ibyte2] = globalMem[ibyte3 + cur->reg[0]];
							   cur->pc += 3;
							   break;
						   }
				case 0x05: {   // LSTORE
							   cur->local_mem[ibyte3] = cur->reg[ibyte2];
							   cur->pc += 3;
							   break;
						   }
				case 0x06: {   // LSTOREi
							   cur->local_mem[ibyte3 + cur->reg[0]] = cur->reg[ibyte2];
							   cur->pc += 3;
							   break;
						   }
				case 0x07: {   // GSTORE
							   globalMem[ibyte3] = cur->reg[ibyte2];
							   /*if (ibyte2 == 7){printf("%d\n", (int)globalMem[ibyte3]&0xff);}*/
							   cur->pc += 3;
							   break;
						   }
				case 0x08: {   // GSTOREi
							   globalMem[ibyte3 + cur->reg[0]] = cur->reg[ibyte2];
							   cur->pc += 3;
							   break;
						   }
				case 0x09: {   // SET
							   cur->reg[ibyte2] = (signed char)ibyte3;
							   cur->pc += 3;
							   break;
						   }
				case 0x0a: {   // ADD
							   cur->reg[ibyte2] = cur->reg[ibyte2] + cur->reg[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x0b: {   // SUB
							   cur->reg[ibyte2] = cur->reg[ibyte2] - cur->reg[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x0c: {   // MUL
							   cur->reg[ibyte2] = cur->reg[ibyte2] * cur->reg[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x0d: {   // DIV
							   cur->reg[ibyte2] = cur->reg[ibyte2] / cur->reg[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x0e: {   // MOD
							   cur->reg[ibyte2] = cur->reg[ibyte2] % cur->reg[ibyte3];
							   cur->pc += 3;
							   break;
						   }
				case 0x0f: {   // BRGZ
							   if (cur->reg[ibyte2] > 0) { cur->pc+=3*(signed char)ibyte3; }
							   else { cur->pc+=3; }
							   break;
						   }
				case 0x10: {   // BRGEZ
							   if (cur->reg[ibyte2] >= 0) { cur->pc+=3*(signed char)ibyte3; }
							   else { cur->pc+=3; }
							   break;
						   }
				case 0x11: {   // BRLZ
							   if (cur->reg[ibyte2] < 0) { cur->pc+=3*(signed char)ibyte3; }
							   else { cur->pc+=3; }
							   break;
						   }
				case 0x12: {   // BRLEZ
							   if (cur->reg[ibyte2] <= 0) { cur->pc+=3*(signed char)ibyte3; }
							   else { cur->pc+=3; }
							   break;
						   }
				case 0x13: {   // BREZ
							   if (cur->reg[ibyte2] == 0) { cur->pc+=3*(signed char)ibyte3; }
							   else { cur->pc+=3; }
							   break;
						   }
				case 0x14: {   // BRA
							   cur->pc+=3*(signed char)ibyte3;
							   break;
						   }
				case 0x15: {   // DOWN
							   globalMem[ibyte3]--;
							   if (globalMem[ibyte3] < 0) {
								   cur->state = BLOCKED;
								   cur->sem = ibyte3;
								   cur_copy = cur;
							   }
							   cur->pc += 3;
							   break;
						   }
				case 0x16: {   // UP
							   globalMem[ibyte3]++;
							   if (globalMem[ibyte3] <= 0) {
								   blc_node = list_locate_blocked(ibyte3);
								   blc_node->state = READY;
								   blc_node->sem = -1;
								   list_move_to(blc_node, D_RDY);
							   }
							   cur->pc += 3;
							   break;
						   }
				case 0x17: {   // YIELD
							   cur->pc +=3;
							   break;
						   }
				case 0x18: {   // SLEEP
							   cur->state = SLEEPING;
							   cur->waket = time(NULL) + cur->reg[ibyte2];
							   cur->pc += 3;
							   break;
						   }
				case 0x19: {   // PRINT
							   printf("%d: %s\n", cur->id, &globalMem[ibyte3]);
							   cur->pc += 3;
							   break;
						   }
				case 0x1A: {   // EXIT
							   cur->state = STOPPED;
							   cur_copy = cur;
							   tasks_stopped++;
							   break;
						   }
				default: {
							 printf("Error: Unknown instruction id\n");
							 exit(1);
						 }
			}
			if ((ibyte1 == 0x17) || (cur->state == STOPPED) || (cur->state == BLOCKED)) {break;}
			i++;
		}
	}
}
