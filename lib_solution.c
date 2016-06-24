#include "lib_solution.h"


//display the list
void printList(struct node *head){
   struct node *ptr = head;
   printf("\n[ ");
	
   //start from the beginning
   while(ptr != NULL)
	{        
	  printf("(%d,%d) ",ptr->key,ptr->data);
	  ptr = ptr->next;
   }
	
   printf(" ]");
}

//insert link at the first location
void insertFirst(struct node *head, int key, int data){
   //create a link
   struct node *link = (struct node*) malloc(sizeof(struct node));
	
   link->key = key;
   link->data = data;
	
   //point it to old first node
   link->next = head;
	
   //point first to new first node
   head = link;
}

//delete first item
struct node* deleteFirst(struct node *head)
{

   //save reference to first link
   struct node *tempLink = head;
	
   //mark next to first link as first 
   head = head->next;
	
   //return the deleted link
   return tempLink;
}

//is list empty
bool isEmpty(struct node *head)
{
   return head == NULL;
}

int length(struct node *head)
{
   int length = 0;
   struct node *current;
	
   for(current = head; current != NULL; current = current->next)
	{
	  length++;
   }
	
   return length;
}

//find a link with given key
struct node* find(struct node *head, int key){

   //start from the first link
   struct node* current = head;

   //if list is empty
   if(head == NULL)
	{
	  return NULL;
   }

   //navigate through list
   while(current->key != key){
	
	  //if it is last node
	  if(current->next == NULL){
		 return NULL;
	  }else {
		 //go to next link
		 current = current->next;
	  }
   }      
	
   //if data found, return the current Link
   return current;
}

//delete a link with given key
struct node* delete(struct node *head, int key){

   //start from the first link
   struct node* current = head;
   struct node* previous = NULL;
	
   //if list is empty
   if(head == NULL){
	  return NULL;
   }

   //navigate through list
   while(current->key != key){
	
	  //if it is last node
	  if(current->next == NULL){
		 return NULL;
	  }else {
		 //store reference to current link
		 previous = current;
		 //move to next link
		 current = current->next;             
	  }
		
   }

   //found a match, update the link
   if(current == head) {
	  //change first to point to next link
	  head = head->next;
   }else {
	  //bypass the current link
	  previous->next = current->next;
   }    
	
   return current;
}


int set_nonblock(int fd){
	int flags;
	#if defined(O_NONBLOCK)
		flags = fcntl(fd, F_GETFL, 0);
		if(flags == -1)
			flags = 0;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	#else
		flags = 1;
		return ioctl(fd, FIOBIO, &flags);
	#endif
}

