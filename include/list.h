#ifndef LIST_H
#define LIST_H

#include <stdint.h>
#include <stdio.h>

// Implementation very much inspired by the linux kernel

#define offset_of(type, member)	((size_t)&((type *)0)->member)
#define container_of(ptr, type, member) ((type *)((void *)(ptr) - offset_of(type, member)))

#define list_for_each_safe(pos, temp, head)         \
    for (pos=(head)->next, temp=pos->next; pos != head; pos=temp, temp=pos->next)

#define list_for_each_prev_safe(pos, temp, head)    \
    for (pos=(head)->prev, temp=pos->prev; pos != head; pos=temp, temp=pos->prev)


typedef struct list_node_ {
    struct list_node_ *next;
    struct list_node_ *prev;
} list_node;

inline int list_is_last(list_node *pos, list_node *head) 
{
    return pos->next == head;
}

inline int list_is_first(list_node *pos, list_node *head) 
{
    return pos->prev == head;
}

inline int list_init(list_node *head)
{
    if (head == NULL)
        return -1;

    head->next = head;
    head->prev = head;

    return 0;
}

inline int list_empty(list_node *head)
{
    return head->next == head && head->prev == head;
}

inline int list_insert_before(list_node *node, list_node *pos)
{
    if (!node || !pos || node == pos)
        return -1;
    
    node->prev = pos->prev;
    node->next = pos;
    pos->prev->next = node;
    pos->prev = node;

    return 0;
}

inline int list_insert_after(list_node *node, list_node *pos)
{
    if (!node || !pos || node == pos)
        return -121;

    node->next = pos->next;
    node->prev = pos;
    pos->next->prev = node;
    pos->next = node;

    return 0;
}

inline int list_remove(list_node *node)
{
    if (!node)
        return -1;

    node->next->prev = node->prev;
    node->prev->next = node->next;

    node->next = NULL;
    node->prev = NULL;

    return 0;
}

inline list_node *list_get(list_node *head, size_t idx)
{
    list_node *node;

    if (list_empty(head))
        return NULL;

    node = head->next;
    while (node != head && idx != 0) {
        node = node->next; 
        idx--;
    }
    
    // idx was out of bounds (should not happen)
    if (node == head)
        return NULL;
    
    return node;
}

inline int list_replace(list_node *old, list_node *new)
{
    if (!old || !new)
        return -1;
    
    new->next = old->next;
    new->next->prev = new;
    new->prev = old->prev;
    new->prev->next = new;

    return 0;
}

inline int list_swap(list_node *node1, list_node *node2)
{
    list_node *pos;

    if (!node1 || !node2)
        return -1;

    if (node1 == node2)
        return 0;
    
    pos = node2->prev;
    if (list_remove(node2) < 0)
        return -119;

    if (list_replace(node1, node2) < 0)
        return -120;

    // Handle the case that the nodes are adjacent
    if (pos == node1)
        pos = node2;
        
    return list_insert_after(node1, pos);
}

#endif
