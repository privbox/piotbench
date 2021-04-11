#ifndef _QUEUE_H
#define _QUEUE_H
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>

#include "lock.h"

#ifndef offsetof
#define offsetof(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
#endif
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})


#define QUEUE_POISON1 ((struct queue_head *)0xCAFEBAB5)


struct queue_head {
	struct queue_head *next;
};


struct queue_root {
	struct queue_head *head;
	lock_t head_lock;

	struct queue_head *tail;
	lock_t tail_lock;

	struct queue_head divider;
};

static inline void init_queue_root(struct queue_root *root) {
	lock_init(&root->head_lock);
	lock_init(&root->tail_lock);

	root->divider.next = NULL;
	root->head = &root->divider;
	root->tail = &root->divider;
}

static inline struct queue_root *alloc_queue_root()
{
	struct queue_root *root = (struct queue_root *) malloc(sizeof(struct queue_root));
	init_queue_root(root);
	return root;
}



static inline void queue_head_init(struct queue_head *head)
{
	head->next = QUEUE_POISON1;
}

static inline void queue_put(struct queue_head *new_, struct queue_root *root)
{
	queue_head_init(new_);
	new_->next = NULL;

	lock(&root->tail_lock);
	root->tail->next = new_;
	root->tail = new_;
	unlock(&root->tail_lock);
}

static inline struct queue_head *queue_get(struct queue_root *root)
{
	struct queue_head *head, *next;

	while (1) {
		lock(&root->head_lock);
		head = root->head;
		next = head->next;
		if (next == NULL) {
			unlock(&root->head_lock);
			return NULL;
		}
		root->head = next;
		unlock(&root->head_lock);

		if (head == &root->divider) {
			queue_put(head, root);
			continue;
		}

		head->next = QUEUE_POISON1;
		return head;
	}
}


#endif // _QUEUE
