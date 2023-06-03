/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"
#include <sys/time.h>
#define SBUFSIZE 128
#define NTHREADS 100
void echo(int connfd);
typedef struct{
    int *buf;   //item array
    int n;      //max slot
    int front;  //buf start
    int rear;   //buf end
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;

struct item{
    int ID;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex,w; // mutex for lock, w for RW problem
};
typedef struct node{
    struct item data;
    struct node* left;
    struct node* right;
}node;
typedef struct node *tree_pointer;
/*for sbuf*/
void sbuf_init(sbuf_t *sp,int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp,int item);
int sbuf_remove(sbuf_t *sp);
sbuf_t sbuf;
/*for thread*/
static int byte_cnt;
static sem_t mutex; // for init echo byte_cnt 
void *thread(void *vargp);
static void init_echo_cnt(void);
void echo_cnt(int connfd);
/*for item(stock)*/
tree_pointer stock_insert(tree_pointer ptr, int id, int left, int price); 
void stock_show(tree_pointer ptr);
void stock_buy(int connfd, tree_pointer ptr, int id, int n);
void stock_sell(int connfd, tree_pointer ptr, int id, int n);
void stock_update(FILE* fp, tree_pointer ptr);
tree_pointer stock_search(tree_pointer ptr, int id);
void show_clear();
char showBuffer[MAXLINE];
tree_pointer root = NULL;
int main(int argc, char **argv) 
{
    int i, listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    int id, left,price;
    FILE *fp = fopen("stock.txt", "r");
    if(fp == NULL){
        perror("Error opening file");
        return(-1);
    }
    while(!feof(fp)){
        fscanf(fp,"%d %d %d", &id,&left,&price);
        root = stock_insert(root, id, left, price);
    }
    fclose(fp);
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }
    
    listenfd = Open_listenfd(argv[1]); // getaddrinfo, socket, bind, listen
    sbuf_init(&sbuf,SBUFSIZE);
    for(i=0; i < NTHREADS;i++)
        Pthread_create(&tid,NULL,thread,NULL);
    while (1) {
	        clientlen = sizeof(struct sockaddr_storage); 
	        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            sbuf_insert(&sbuf, connfd);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
    }
    sbuf_deinit(&sbuf);
    exit(0);
}
/* $end echoserverimain */
void sbuf_init(sbuf_t *sp,int n){
    sp->buf = Calloc(n,sizeof(int));
    sp->n = n ;
    sp->front = sp->rear=0;
    Sem_init(&sp->mutex,0,1);
    Sem_init(&sp->slots,0,n);
    Sem_init(&sp->items,0,0);
}
void sbuf_deinit(sbuf_t *sp){
    Free(sp->buf);
}
void sbuf_insert(sbuf_t *sp,int item){
    P(&sp->slots); // Wait for available slot
    P(&sp->mutex); // Lock the buffer
    sp->buf[(++sp->rear)%(sp->n)] = item; // Insert
    V(&sp->mutex); // Unlock
    V(&sp->items); //item semaphore ++
}
int sbuf_remove(sbuf_t *sp){
    int item;
    P(&sp->items); // Wait for available item
    P(&sp->mutex); // Lock the buffer
    item = sp->buf[(++sp->front)%(sp->n)]; // Insert
    V(&sp->mutex); // Unlock
    V(&sp->slots); //slot semaphore ++
    return item;
}
void *thread(void *vargp){
    Pthread_detach(pthread_self());
    while(1){
        int connfd = sbuf_remove(&sbuf);
        echo_cnt(connfd); // thread에서 echo_cnt 호출
        Close(connfd);
        FILE* fp; // update stock.txt
        fp= fopen("stock.txt", "w");
        stock_update(fp, root);
        fclose(fp);
    }
}
static void init_echo_cnt(void){
    Sem_init(&mutex, 0, 1);
    byte_cnt=0;
}
void echo_cnt(int connfd){ // command control
    int n;
    char buf[MAXLINE];
    char tmp[MAXLINE]; // for buy, sell , temp string
    rio_t rio;
    int sID;
    int sNum;
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_echo_cnt);
    Rio_readinitb(&rio,connfd);
    while((n=Rio_readlineb(&rio,buf, MAXLINE))!=0){
        P(&mutex); // byte count mutex
        byte_cnt += n;
        printf("server received %d bytes\n",n);
        V(&mutex);
        if(!strncmp(buf, "show", 4)){
            stock_show(root);
            Rio_writen(connfd,showBuffer,MAXLINE);
            show_clear();
        }
        else if(!strncmp(buf, "buy", 3)){
            sscanf(buf, "%s %d %d\n", tmp, &sID, &sNum);
            stock_buy(connfd, root, sID, sNum);
        }
        else if(!strncmp(buf, "sell", 4)){
            sscanf(buf, "%s %d %d\n", tmp, &sID, &sNum);
            stock_sell(connfd, root, sID, sNum);
        }
        else if(!strncmp(buf, "exit", 4)){
            sscanf(buf, "%s %d %d\n", tmp, &sID, &sNum);
            Rio_writen(connfd,tmp,MAXLINE);
            break;
        }
    }
}
tree_pointer stock_insert(tree_pointer ptr, int id, int left, int price){
    if(ptr==NULL){// if first insert
        ptr = (tree_pointer)malloc(sizeof(*ptr));
        ptr->left = NULL;
        ptr->right = NULL;
        ptr->data.ID = id;
        ptr->data.left_stock = left;
        ptr->data.price = price;
        ptr->data.readcnt = 0;
        Sem_init(&ptr->data.mutex,0,1); // mutex,w set 1
        Sem_init(&ptr->data.w,0,1);
    }
    else if(ptr->data.ID > id){
        ptr->left = stock_insert(ptr->left,id,left,price);
    }
    else if(ptr->data.ID < id){
        ptr->right = stock_insert(ptr->right,id,left,price);
    }
    return ptr;
}
void stock_show(tree_pointer ptr){
    char temp[MAXLINE] = {'\0',};
    if(ptr == NULL) return;
    P(&(ptr->data.mutex));
    ptr->data.readcnt++;
    if(ptr->data.readcnt ==1)
        P(&(ptr->data.w));
    V(&(ptr->data.mutex));
    sprintf(temp,"%d %d %d\n",ptr->data.ID, ptr->data.left_stock,ptr->data.price);
    strcat(showBuffer,temp);
    P(&(ptr->data.mutex));
    ptr->data.readcnt--;
    if(ptr->data.readcnt==0)
        V(&(ptr->data.w));
    V(&(ptr->data.mutex));
    stock_show(ptr->left);
    stock_show(ptr->right);
}
void stock_buy(int connfd, tree_pointer ptr, int id, int n){
    char temp[MAXLINE]={'\0',};
    ptr = stock_search(ptr, id);
    P(&(ptr->data.w)); //buy lock
    if(ptr->data.left_stock >= n){
        ptr->data.left_stock-=n;
        sprintf(temp, "[buy] success\n");
        Rio_writen(connfd,temp,MAXLINE);
    }
    else{
        sprintf(temp, "Not enough left stocks\n");
        Rio_writen(connfd,temp,MAXLINE);
    }
    V(&(ptr->data.w)); // buy unlock
}
void stock_sell(int connfd, tree_pointer ptr, int id, int n){
    char temp[MAXLINE];
    ptr = stock_search(ptr, id);
    P(&(ptr->data.w)); //sell lock
    ptr->data.left_stock+=n;
    V(&(ptr->data.w)); //sell lock
    sprintf(temp, "[sell] success\n");
    Rio_writen(connfd,temp,MAXLINE);
}
tree_pointer stock_search(tree_pointer ptr, int id){ // node search
    if(ptr == NULL || ptr->data.ID == id){
        return ptr;
    }
    else if(id < ptr->data.ID){
        return stock_search(ptr->left, id);
    }
    else{
        return stock_search(ptr->right, id);
    }
}
void stock_update(FILE* fp, tree_pointer ptr){
    if(ptr == NULL) return;
    P(&(ptr->data.mutex));
    ptr->data.readcnt++;
    if(ptr->data.readcnt ==1)
        P(&(ptr->data.w));
    V(&(ptr->data.mutex));
    fprintf(fp, "%d %d %d\n",ptr->data.ID, ptr->data.left_stock,ptr->data.price);
    P(&(ptr->data.mutex));
    ptr->data.readcnt--;
    if(ptr->data.readcnt==0)
        V(&(ptr->data.w));
    V(&(ptr->data.mutex));
    stock_update(fp, ptr->left);
    stock_update(fp, ptr->right);
}
void show_clear(){
    for(int i=0; i < MAXLINE;i++){
        showBuffer[i] = '\0';
    }
}