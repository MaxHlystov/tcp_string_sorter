/*
Some auxilary function.
1. Functions for working with list.
2. Function for set descriptor to nonblock state.
*/

#ifndef __LIB_SOLUTION
#define __LIB_SOLUTION


	#include <stdbool.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <sys/ioctl.h>
	#include <fcntl.h>
	
	
	struct node {
	   void* data;
	   int key;
	   struct node *next;
	};

	//display the list
	void printList(struct node **head);
	
	//insert link at the first location
	void push(struct node **head, int key, void* data);

	//delete first item
	struct node* pop(struct node **head);

	//is list empty
	bool isEmpty(struct node **head);

	// the length on the list
	int length(struct node **head);

	//find a link with given key
	struct node* find(struct node **head, int key);

	//delete a link with given key
	struct node* deleteNode(struct node **head, int key);

	// set descriptor nonblock
	int set_nonblock(int fd);
	
#endif
