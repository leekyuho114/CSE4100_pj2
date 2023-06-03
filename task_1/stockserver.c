/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

void echo(int connfd);

typedef struct{
    int maxfd;
    fd_set read_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio [FD_SETSIZE];
}pool;

struct item{
    int ID;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex;
};
typedef struct node{
    struct item data;
    struct node* left;
    struct node* right;
}node;
typedef struct node *tree_pointer;
/*for select*/
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
/*for item(stock)*/
tree_pointer stock_insert(tree_pointer ptr, int id, int left, int price); 
void stock_show(tree_pointer ptr);
void stock_buy(int connfd, tree_pointer ptr, int id, int n);
void stock_sell(int connfd, tree_pointer ptr, int id, int n);
void stock_update(FILE* fp, tree_pointer ptr);
tree_pointer stock_search(tree_pointer ptr, int id);
void show_clear();
char showBuffer[MAXLINE];
int byte_cnt = 0; //counts total bytes received by server
tree_pointer root = NULL;
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
    static pool pool;
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
    init_pool(listenfd, &pool);
    while (1) {
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set,NULL,NULL,NULL);
        if(FD_ISSET(listenfd, &pool.ready_set)){
	        clientlen = sizeof(struct sockaddr_storage); 
	        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
            add_client(connfd,&pool);
        }
        check_clients(&pool);
    }
    exit(0);
}
/* $end echoserverimain */
void init_pool(int listenfd, pool *p){
    int i;
    p->maxi = -1;
    for (i=0; i<FD_SETSIZE; i++){
        p->clientfd[i] = -1; 
    }
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}
void add_client(int connfd, pool *p){
    int i;
    p->nready--;
    for(i=0 ; i<FD_SETSIZE; i++)//find available slot
        if(p->clientfd[i]<0){
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i],connfd);
        
            FD_SET(connfd, &p->read_set);

            if(connfd > p->maxfd)
                p->maxfd =connfd;
            if(i > p->maxi)
                p->maxi = i;
            break;
        }

    if(i == FD_SETSIZE)
        app_error("add_client error: Too many clients");
}
void check_clients(pool *p){ // command control
    int i, connfd,n;
    char buf[MAXLINE];
    char tmp[MAXLINE]; // for buy, sell , temp string
    rio_t rio;
    int sID;
    int sNum;
    for(i=0; (i<=p->maxi) && (p->nready>0);i++){
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        if ((connfd>0)&&(FD_ISSET(connfd, &p->ready_set))){
            p->nready--;
            if((n=Rio_readlineb(&rio,buf,MAXLINE))!=0){
                byte_cnt += n;
                printf("server received %d bytes\n", n);
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
                    Close(connfd);
                    FD_CLR(connfd,&p->read_set);
                    p->clientfd[i]=-1;
                    FILE* fp; // file update
                    fp= fopen("stock.txt", "w");
                    stock_update(fp, root);
                    fclose(fp);
                    return;
                }
            }
            else{//client가 종료된 경우
                Close(connfd);
                FD_CLR(connfd,&p->read_set);
                p->clientfd[i]=-1;
                FILE* fp; // file update
                fp= fopen("stock.txt", "w");
                stock_update(fp, root);
                fclose(fp);
            }
        }
    }
}
tree_pointer stock_insert(tree_pointer ptr, int id, int left, int price){
    if(ptr==NULL){
        ptr = (tree_pointer)malloc(sizeof(*ptr));
        ptr->left = NULL;
        ptr->right = NULL;
        ptr->data.ID = id;
        ptr->data.left_stock = left;
        ptr->data.price = price;
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
    sprintf(temp,"%d %d %d\n",ptr->data.ID, ptr->data.left_stock,ptr->data.price);
    strcat(showBuffer,temp);
    stock_show(ptr->left);
    stock_show(ptr->right);
}
void stock_buy(int connfd, tree_pointer ptr, int id, int n){
    char temp[MAXLINE]={'\0',};
    ptr = stock_search(ptr, id);
    if(ptr->data.left_stock >= n){
        ptr->data.left_stock-=n;
        sprintf(temp, "[buy] success\n");
        Rio_writen(connfd,temp,MAXLINE);
    }
    else{
        sprintf(temp, "Not enough left stocks\n");
        Rio_writen(connfd,temp,MAXLINE);
    }
}
void stock_sell(int connfd, tree_pointer ptr, int id, int n){
    char temp[MAXLINE];
    ptr = stock_search(ptr, id);
    ptr->data.left_stock+=n;
    sprintf(temp, "[sell] success\n");
    Rio_writen(connfd,temp,MAXLINE);
}
tree_pointer stock_search(tree_pointer ptr, int id){
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
    fprintf(fp, "%d %d %d\n",ptr->data.ID, ptr->data.left_stock,ptr->data.price);
    stock_update(fp, ptr->left);
    stock_update(fp, ptr->right);
}
void show_clear(){
    for(int i=0; i < MAXLINE;i++){
        showBuffer[i] = '\0';
    }
}
