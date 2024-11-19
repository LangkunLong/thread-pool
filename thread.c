#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include <stdbool.h>
#include <sys/time.h>

//thread states
#define CREATED         0
#define WAITING         1
#define RUNNING         2
#define TERMINATED      3
#define EXITED          4
#define SLEEPING        5

struct node{
    Tid thread_id;
    struct node* next;
};

struct wait_queue{
    /* ... Fill this in Lab 3 ... */
    struct node* head;
};

/* This is the thread control block */
struct thread{
    Tid thread_id;
    int thread_state;
    void* stack_ptr;
    ucontext_t thread_context;
    struct wait_queue* wq;
    int exit_code;
};
/* This is the wait queue structure frame
 * FIFO: need front and end pointer
 * use a circular queue so we don't run into static memory problems */
struct linked_list_queue{
    struct node* head;
    struct node* tail;
};
 
/*GLOBAL VARAIBLES*/
int threads_in_queue; //keeps track the current number of threads, cannot exceed the max num
//int numCreatedThreads;
int current_running_thread; //only 1 thread runs at a time
struct thread threads[THREAD_MAX_THREADS]; //static memory capable to store all the threads
struct linked_list_queue queue;
int num_return = 0;
void thread_awaken(Tid);

//helper function to see if thread is in queue
bool in_queue(Tid threadID){
    int enabled = interrupts_off();
    struct node* cur = queue.head;
    
    while(cur != NULL){
        if(cur->thread_id == threadID)
            return true;
        cur = cur->next;
    }
    interrupts_set(enabled);
    return false;
}

void remove_item(Tid thread_id){
    int enabled = interrupts_off();
    
    if(queue.head == NULL){
        interrupts_set(enabled);
        return;
    }
    
    struct node* temp = queue.head;
    if(queue.head->thread_id == thread_id){
        queue.head = queue.head->next;
        temp->next = NULL;
        free(temp);
        interrupts_set(enabled);
        return;
    }
    struct node* cur = queue.head; 
    struct node* pre = queue.head;
    
    while(cur != NULL){
        if(cur->thread_id == thread_id){
            pre->next = cur->next;
            free(cur);
            interrupts_set(enabled);
            return;
        }
        pre = cur;
        cur = cur->next;
    }
    interrupts_set(enabled);
}

//pop the first item off the queue
Tid pop_head(){
    int enabled = interrupts_off();
    
    if(queue.head == NULL){
        interrupts_set(enabled);    
        return THREAD_NONE;
    }
    
    struct node* temp = queue.head;
    queue.head = queue.head->next;
    // if(queue.head == NULL)
        // queue.tail = NULL; //if head becomes null queue is now empty
    
    temp->next = NULL;
    Tid wanted_thread = temp->thread_id;
    free(temp);
    threads_in_queue--;
    //numCreatedThreads--;
    interrupts_set(enabled);
    return wanted_thread;
}

//pop any node, can be in the middle
Tid pop_any(Tid wanted_thread){
    int enabled = interrupts_off();
    
    if(queue.head == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    
    if(wanted_thread == queue.head->thread_id) //disabled interrupts in pop_head()
        return pop_head();
    
    struct node* cur = queue.head; 
    struct node* prev = queue.head;
    while(cur != NULL){
        if(cur->thread_id == wanted_thread){
            prev->next = cur->next;
            free(cur);
            threads_in_queue--;
            //numCreatedThreads--;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    if(cur == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    interrupts_set(enabled);
    return wanted_thread;
}

//push at the tail of the list
void push(Tid thread_pushed){
    int enabled = interrupts_off();
    
    struct node* new_node = (struct node*)malloc(sizeof(struct node));
    new_node->thread_id = thread_pushed;
    new_node->next = NULL; //insert at tail
    
    if(queue.head == NULL){ //no prior entry
        queue.head = new_node;
        // queue.tail = queue.head;
    }
    else{
        // queue.tail->next = new_node;
        // queue.tail = new_node;
        struct node* cur = queue.head;
        struct node* prev = queue.head;
        while(cur){
            prev=cur;
            cur=cur->next;
        }
        prev->next=new_node;
        
    }
    threads_in_queue++;
    interrupts_set(enabled);
}

void printlist(){
    int enabled = interrupts_off();
    struct node* temp = queue.head;
    while(temp != NULL){
        printf("%d\n",temp->thread_id);
        temp = temp->next;
    }
    interrupts_set(enabled);
}

void
thread_init(void)
{
	/* creates the first thread, put into queue?
         * first thread don't need to allocate stack because it will run on the user stack allocated for this kernel thread by OS  */
    int enabled = interrupts_off();
    current_running_thread = 0; //thread id of 0
    //numCreatedThreads = 0;
    
    //&queue = (struct linked_list_queue*)malloc(sizeof(struct linked_list_queue));
    queue.head = NULL;
    queue.tail = NULL;
    
    //threads[0].thread_id = 0;
    for(int thread_num = 0; thread_num < THREAD_MAX_THREADS ; thread_num++){     //initialize all threads 
        threads[thread_num].thread_state = CREATED;
        threads[thread_num].thread_id = thread_num;
        threads[thread_num].wq = NULL;
        threads[thread_num].exit_code = 0;
    }
    threads[0].thread_state = RUNNING; //first thread running
    threads[0].wq = wait_queue_create();
    
    //struct ucontext_t cur_context;
    int err = getcontext(&(threads[0].thread_context)); //get the context of first thread
    assert(!err); //why? 
    
    //threads[0].thread_context = cur_context;   
    interrupts_set(enabled);
}

Tid
thread_id()
{
    /* thread id ranges from 0 - 1023
       since we only have 1 thread running at a time, we can just update global variable*/
    int enabled = interrupts_off();
    if(current_running_thread >= 0){
        interrupts_set(enabled);
	return current_running_thread;
    }
    else{
        interrupts_set(enabled);
        return THREAD_NONE;
    }
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
    //don't want it running the stub function; can't change register state for some reason in thread kill
    //if(threads[current_running_thread].thread_state == TERMINATED){
    //    threads[current_running_thread].thread_state = EXITED;
    //    thread_exit(9);
    //}
    int enabled = interrupts_on();
    thread_main(arg); // call thread_main() function with arg
    //printlist();
    interrupts_set(enabled);
    thread_exit(9); // return from this if the queue is empty( no more thread to run)
    exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
    int enabled = interrupts_off();
    
    struct ucontext_t cur_context;
    int err = getcontext(&cur_context);
    assert(!err);
    
    void* stack_ptr = malloc(THREAD_MIN_STACK); //point to lowest memory address, all the memory that need to be freed 
    if(stack_ptr == NULL){
        printf("I am here");
        interrupts_set(enabled);
        return THREAD_NOMEMORY;
    }
    
    Tid new_thread;
    bool availThread = false;
    for(int i = 1; i < THREAD_MAX_THREADS ; i++){
        if(threads[i].thread_state == CREATED || threads[i].thread_state == EXITED){
            new_thread = i;
            availThread = true;
            break;
        }
    }
    if(availThread == false){
        //printf("I shouldn't be here");
        interrupts_set(enabled);
        return THREAD_NOMORE;
    }
    
    //stack_ptr = stack_ptr
    threads[new_thread].stack_ptr = stack_ptr;
    threads[new_thread].thread_id = new_thread;
    threads[new_thread].thread_state = WAITING;
    threads[new_thread].wq = wait_queue_create();
    
    //move sp to the top, then point to next empty memory
    cur_context.uc_stack.ss_sp = stack_ptr + THREAD_MIN_STACK - 8; 
    cur_context.uc_stack.ss_size = THREAD_MIN_STACK - 8;
    cur_context.uc_stack.ss_flags = 0;
    cur_context.uc_mcontext.gregs[REG_RIP] = (unsigned long)&thread_stub;  //first function it runs is the stub function 
    cur_context.uc_mcontext.gregs[REG_RSP] = (unsigned long)(stack_ptr + THREAD_MIN_STACK - 8);
    cur_context.uc_mcontext.gregs[REG_RDI] = (unsigned long)fn; //argument registers
    cur_context.uc_mcontext.gregs[REG_RSI] = (unsigned long)parg;
    
    threads[new_thread].thread_context = cur_context;

    //if(threads_in_queue == THREAD_MAX_THREADS){
    //    printf("HERE");
    //    return THREAD_NOMEMORY;
    //}
    
    push(new_thread); //push on queue
    
    interrupts_set(enabled);
    return new_thread;
}

void kill_TERMINATED_threads(){
    int enabled = interrupts_off();
    for(int i = 1; i < THREAD_MAX_THREADS; i++){
        if(threads[i].thread_state == TERMINATED && i != current_running_thread){
            if(threads[i].stack_ptr != NULL){
                free(threads[i].stack_ptr);
                threads[i].stack_ptr = NULL;
                threads[i].thread_state = EXITED;
            //}
                remove_item(i); //remove from queue
            }
        }
    }
    interrupts_set(enabled);
}

//should return a new thread id if passed in a new thread
Tid thread_yield(Tid want_tid){
    int enabled = interrupts_off();
    
    kill_TERMINATED_threads();
    //printf("current thread: %d\n", current_running_thread);
    Tid new_threadID;
    
    if(want_tid == THREAD_ANY){ //FIFO from queue 
        if(queue.head == NULL){ //if queue is empty
            interrupts_set(enabled);
            return THREAD_NONE;
        }
        else{
            new_threadID = pop_head();
            threads[new_threadID].thread_state = RUNNING;
        }
   
    }else if(want_tid == THREAD_SELF || want_tid == thread_id()){ //current running thread {
        interrupts_set(enabled);
        return thread_id();
    }
    else{ //want specific thread in the queue
        /*bool valid = (want_tid > 0 && want_tid < THREAD_MAX_THREADS);
        if(valid){
            if(threads[want_tid].thread_state == WAITING || threads[want_tid].thread_state == EXITED){
                
                if(pop_any(want_tid) == THREAD_INVALID)
                    return THREAD_INVALID;
                
                new_threadID = pop_any(want_tid); //new_threadID should == want_tid
                threads[new_threadID].thread_state = RUNNING;
            }else
                return THREAD_INVALID;
        }else
            return THREAD_INVALID;        */
        
        if(in_queue(want_tid) ==  false){
            interrupts_set(enabled);
            return THREAD_INVALID;
        }
        
        new_threadID = pop_any(want_tid);
        threads[new_threadID].thread_state = RUNNING;
    }
    
    volatile int set_context_called = 0;
    int err = getcontext(&threads[current_running_thread].thread_context); //get context of current running thread
    //struct ucontext_t cur_context = threads[current_running_thread].thread_context;
    //will get signal blocked in the get context
    
    assert(!err);
    
    if(set_context_called == 0){
        //threads[current_running_thread].thread_context = cur_context; //store current context
        if(threads[current_running_thread].thread_state != TERMINATED && threads[current_running_thread].thread_state != SLEEPING){
            threads[current_running_thread].thread_state = WAITING;
        //printf("%d\n", thread_id());
        //printf("current thread: %d\n", current_running_thread);
            push(current_running_thread); //push to last entry of queue
        }
        
        //printlist();
        current_running_thread = new_threadID;
        //printf("new thread: %d\n", current_running_thread);
        threads[current_running_thread].thread_state = RUNNING;
        //cur_context = threads[new_threadID].thread_context; //update current context 
        set_context_called = 1;
        setcontext(&threads[new_threadID].thread_context);
    }//else if (set_context_called == 1)
        
    //kill_TERMINATED_threads();
    interrupts_set(enabled);
    return new_threadID;
    
    //return current_running_thread;
}

void
thread_exit(int exit_code)
{
    //tell the thread that is about to exit to switch to another thread
    //printlist();
    int enabled = interrupts_off();
    
    threads[current_running_thread].exit_code = exit_code;
    
    thread_wakeup(threads[current_running_thread].wq, 1);
    
    threads[current_running_thread].thread_state = TERMINATED;
    
    thread_yield(THREAD_ANY);
    
    interrupts_set(enabled);
    return;
}

//check if there's any child threads before killing parent
Tid
thread_kill(Tid tid)
{
    int enabled = interrupts_off();
    if(tid < 0 || tid > THREAD_MAX_THREADS - 1){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    
    //if there's thread waiting, we cannot kill it
    if(threads[tid].wq !=  NULL || threads[tid].wq->head != NULL){
        //printf("here?\n");
        interrupts_set(enabled);
        return tid;
    }
    
    //threads[tid].thread_context.uc_mcontext.gregs[REG_RIP] = (unsigned long)&thread_exit; //when the killed thread runs it will run into thread_exit
    //threads[tid].thread_context.uc_mcontext.gregs[REG_RDI] = 9; apparently this doesn't work
    if(threads[tid].thread_state == SLEEPING){
        thread_awaken(tid);
    }
    threads[tid].thread_state = TERMINATED;
    interrupts_set(enabled);
    return tid;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
    int enabled = interrupts_off();
    struct wait_queue *wq;

    wq = malloc(sizeof(struct wait_queue));
    assert(wq);
    //wq->head = NULL; //initialize to null at first 

    interrupts_set(enabled);
    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
    struct node* cur = wq->head;
    struct node* temp = wq->head;
    while(cur){
        temp = cur;
        cur = cur->next;
        free(temp);
    }
    free(wq);
}

void wait_queue_print(struct wait_queue *wait_queue){
    int enabled = interrupts_off();
    struct node* temp = wait_queue->head;
    while(temp){
        printf("%d\n",temp->thread_id);
        temp = temp->next;
    }
    interrupts_set(enabled);
}

void wait_queue_push(struct wait_queue *wait_queue){
    
    int enabled = interrupts_off();
    
    struct node* new_node = (struct node*)malloc(sizeof(struct node));
    new_node->thread_id = thread_id();
    new_node->next = NULL; //insert at tail
    
    if(wait_queue->head == NULL){ //no prior entry
        wait_queue->head = new_node;
        // queue.tail = queue.head;
    }
    else{
        // queue.tail->next = new_node;
        // queue.tail = new_node;
        struct node* cur = wait_queue->head;
        struct node* prev = wait_queue->head;
        while(cur){
            prev=cur;
            cur=cur->next;
        }
        prev->next=new_node;
        
    }
    threads_in_queue++;
    interrupts_set(enabled);
}

//pop from the front, fifo order
Tid wait_queue_pop(struct wait_queue *w_queue){
    int enabled = interrupts_off();
    
    if(w_queue->head == NULL){
        interrupts_set(enabled);    
        return THREAD_NONE;
    }
    
    struct node* temp = w_queue->head;
    w_queue->head = w_queue->head->next;
    // if(queue.head == NULL)
        // queue.tail = NULL; //if head becomes null queue is now empty
    
    temp->next = NULL;
    Tid wanted_thread = temp->thread_id;
    free(temp);
    //numCreatedThreads--;
    interrupts_set(enabled);
    return wanted_thread;
}
//helper function to see if thread is in wait queue
bool in_wait_queue(Tid threadID, struct wait_queue* w_queue){
    int enabled = interrupts_off();
    struct node* cur = w_queue->head;
    
    while(cur != NULL){
        if(cur->thread_id == threadID)
            return true;
        cur = cur->next;
    }
    interrupts_set(enabled);
    return false;
}

Tid wq_pop_any(Tid wanted_thread, struct wait_queue* w_queue){
    int enabled = interrupts_off();
    
    if(w_queue->head == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    
    if(wanted_thread == w_queue->head->thread_id) //disabled interrupts in pop_head()
        return pop_head();
    
    struct node* cur = w_queue->head; 
    struct node* prev = w_queue->head;
    while(cur != NULL){
        if(cur->thread_id == wanted_thread){
            prev->next = cur->next;
            free(cur);
            threads_in_queue--;
            //numCreatedThreads--;
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    if(cur == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    interrupts_set(enabled);
    return wanted_thread;
}

//this is a helper function to wake up a a sleeping thread that need to be killed 
//remove from wait queue, put into ready queue to be killed 
void thread_awaken(Tid tid){
    int enabled = interrupts_off();
    bool in_wait = false;
    for(int threadnum = 0; threadnum < THREAD_MAX_THREADS; threadnum++){
        if(in_wait_queue(tid, threads[threadnum].wq)){
            in_wait = true;
            wq_pop_any(tid, threads[threadnum].wq);
        }
    }
    if(in_wait){
        push(tid);
    }
    interrupts_set(enabled);
}

Tid
thread_sleep(struct wait_queue *w_queue)
{
    int enabled = interrupts_off();
    if(w_queue == NULL){ //wait queue not even initialized
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    if(queue.head == NULL){ //ready queue both empty
        interrupts_set(enabled);
        return THREAD_NONE;
    }
    wait_queue_push(w_queue);     //push current running thread into wait_queue
    //wait_queue_print(w_queue);
    //int returned_thread = pop_any(current_running_thread); //remove from ready queue
    //printf("returned thread: %d", returned_thread);
    threads[current_running_thread].thread_state = SLEEPING;
    int thread_id = thread_yield(THREAD_ANY); //run any other thread 
    
    interrupts_set(enabled);
    return thread_id;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *w_queue, int all)
{
    //take threads off the wait queue, put into ready queue
    int enabled = interrupts_off();
    if(w_queue == NULL){
        interrupts_set(enabled);
        return 0;
    }
    int woke_threads = 0;
    if(all == 1){ //all threads are woken up
        while(w_queue->head){
            Tid thread_id = wait_queue_pop(w_queue);   //remove from wait queue
            if(thread_id == THREAD_NONE){
                interrupts_set(enabled);
                return 0;
            }
            //printf("in thread wakeup");
            push(thread_id);                    //put into ready queue 
            woke_threads++;
        }
    }else if(all == 0){ //wake up 1 thread in fifo order
        Tid thread_id = wait_queue_pop(w_queue); //remove from wait queue
            if(thread_id == THREAD_NONE){
                interrupts_set(enabled);
                return 0;
            }
            push(thread_id);                    //put into ready queue 
            woke_threads++;
    }
    interrupts_set(enabled);
    return woke_threads;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
    int enabled = interrupts_off();
    //printf("inside created thread\n");
    if(tid == current_running_thread || tid < 0 || tid > THREAD_MAX_THREADS || tid == THREAD_SELF){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    if(threads[tid].thread_state == EXITED || threads[tid].thread_state == TERMINATED){
        if(exit_code != NULL)
            *exit_code = threads[tid].exit_code;
        interrupts_set(enabled);
        if(__sync_fetch_and_add(&num_return, 1) == 1)
            return tid;
        else
            return THREAD_INVALID;
    }
    /*
    if(threads[tid].wq == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }else{
        if(threads[tid].wq->head == NULL){
            //current thread sleeps on the target thread's wait queue
            thread_sleep(threads[tid].wq);
            if(exit_code != NULL)
                *exit_code = threads[tid].exit_code;
            interrupts_set(enabled);
            return tid;
        }else{
            interrupts_set(enabled);
            return THREAD_INVALID;
        }
    }*/
    if(threads[tid].wq == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }else{
        thread_sleep(threads[tid].wq);
        //printf("after sleep in created threads\n");
        //wait_queue_print(threads[tid].wq);
        if(exit_code != NULL)
            *exit_code = threads[tid].exit_code;
        interrupts_set(enabled);
        //atomic operation, only return 1 of the id 
        if(__sync_fetch_and_add(&num_return, 1) == 1)
            return tid;
        else
            return THREAD_INVALID;
    }
}

struct lock {
	/* ... Fill this in ... */
    //wait queue that other threads get stuck in 
    struct wait_queue* lock_wq;
    Tid current_threadID;
};

struct lock *
lock_create()
{
    int enabled = interrupts_off();
    struct lock *lock = malloc(sizeof(struct lock));
    assert(lock);
    lock->lock_wq = wait_queue_create();
    lock->current_threadID = THREAD_NONE;
    interrupts_set(enabled);
    return lock;
}

void
lock_destroy(struct lock *lock)
{
    int enabled = interrupts_off();
    wait_queue_destroy(lock->lock_wq);
    free(lock);
    interrupts_set(enabled);
}

//suspend thread until lock is free
//if thread without a lock try to get it, it gets stuck
void
lock_acquire(struct lock *lock)
{
    //use sync_val_compare_and_swap
    int enabled = interrupts_off();
    //compare to see if lock has become freed, if not THREAD_NONE, we wait. If available, we give it to current running thread 
    while(__sync_val_compare_and_swap(&lock->current_threadID,THREAD_NONE,current_running_thread) != THREAD_NONE){
        thread_sleep(lock->lock_wq);
    }
    interrupts_set(enabled);
    return;
}

void
lock_release(struct lock *lock)
{
    int enabled = interrupts_off();
    if(lock->current_threadID == current_running_thread){   //check to see if it is calling thread freeing it
        lock->current_threadID = THREAD_NONE;
        thread_wakeup(lock->lock_wq, 1); 
    }
    interrupts_set(enabled);
}

struct cv {
	/* ... Fill this in ... */
    struct wait_queue* cv_wq;
};

struct cv *
cv_create()
{
    int enabled = interrupts_off();
    struct cv* conditional_var = malloc(sizeof(struct cv));
    conditional_var->cv_wq = wait_queue_create();
    interrupts_set(enabled);
    return conditional_var;
}

void
cv_destroy(struct cv *cv)
{
    int enabled = interrupts_off();
    wait_queue_destroy(cv->cv_wq);
    free(cv);
    interrupts_set(enabled);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    int enabled = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);
    //make sure calling thread has lock
    if(lock->current_threadID == current_running_thread){
        lock_release(lock);
        thread_sleep(cv->cv_wq);
        //there is a while loop in the lock acquire function, so we don't have to wait here
        lock_acquire(lock);
    }
    interrupts_set(enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    int enabled = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);
    if(lock->current_threadID == current_running_thread){
        thread_wakeup(cv->cv_wq,0);
        //lock_release(lock);
    }
    interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    int enabled = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);
    if(lock->current_threadID == current_running_thread){
        thread_wakeup(cv->cv_wq, 1);
    }
    interrupts_set(enabled);
}
