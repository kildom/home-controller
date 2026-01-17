#ifndef LIST_HH
#define LIST_HH

#include "Utils.hh"

class ListItem
{
public:
    ListItem* next;
    ListItem* prev;

    void addBefore(ListItem* itemToAdd) {
        itemToAdd->next = this;
        itemToAdd->prev = prev;
        prev->next = itemToAdd;
        prev = itemToAdd;
    }

    void addAfter(ListItem* itemToAdd) {
        itemToAdd->next = next;
        itemToAdd->prev = this;
        next->prev = itemToAdd;
        next = itemToAdd;
    }

    void remove() {
        prev->next = next;
        next->prev = prev;
    }
};

class List: private ListItem
{
public:

    List() {
        next = this;
        prev = this;
    }

    void addFirst(ListItem* item) {
        addAfter(item);
    }

    void addLast(ListItem* item) {
        addBefore(item);
    }

    ListItem* first() {
        return next;
    }

    ListItem* last() {
        return prev;
    }

    ListItem* listEnd() {
        return this;
    }
};

#endif // LIST_HH