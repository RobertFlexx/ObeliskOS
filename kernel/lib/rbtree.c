/*
 * Obelisk OS - Red-Black Tree Implementation
 * From Axioms, Order.
 *
 * Based on Linux kernel rbtree implementation.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>

#define RB_RED      0
#define RB_BLACK    1

#define rb_parent(r)    ((struct rb_node *)((r)->__rb_parent_color & ~3))
#define rb_color(r)     ((r)->__rb_parent_color & 1)
#define rb_is_red(r)    (!rb_color(r))
#define rb_is_black(r)  rb_color(r)

static inline void rb_set_parent(struct rb_node *rb, struct rb_node *p) {
    rb->__rb_parent_color = (rb->__rb_parent_color & 3) | (unsigned long)p;
}

static inline void rb_set_color(struct rb_node *rb, int color) {
    rb->__rb_parent_color = (rb->__rb_parent_color & ~1) | color;
}

static inline void rb_set_parent_color(struct rb_node *rb, struct rb_node *p, int color) {
    rb->__rb_parent_color = (unsigned long)p | color;
}

/*
 * rb_link_node - Link a node into the tree
 */
void rb_link_node(struct rb_node *node, struct rb_node *parent,
                  struct rb_node **rb_link) {
    node->__rb_parent_color = (unsigned long)parent;
    node->rb_left = node->rb_right = NULL;
    
    *rb_link = node;
}

/*
 * Rotate left
 */
static void rb_rotate_left(struct rb_node *node, struct rb_root *root) {
    struct rb_node *right = node->rb_right;
    struct rb_node *parent = rb_parent(node);
    
    node->rb_right = right->rb_left;
    if (right->rb_left) {
        rb_set_parent(right->rb_left, node);
    }
    
    right->rb_left = node;
    rb_set_parent(right, parent);
    
    if (parent) {
        if (node == parent->rb_left) {
            parent->rb_left = right;
        } else {
            parent->rb_right = right;
        }
    } else {
        root->rb_node = right;
    }
    
    rb_set_parent(node, right);
}

/*
 * Rotate right
 */
static void rb_rotate_right(struct rb_node *node, struct rb_root *root) {
    struct rb_node *left = node->rb_left;
    struct rb_node *parent = rb_parent(node);
    
    node->rb_left = left->rb_right;
    if (left->rb_right) {
        rb_set_parent(left->rb_right, node);
    }
    
    left->rb_right = node;
    rb_set_parent(left, parent);
    
    if (parent) {
        if (node == parent->rb_right) {
            parent->rb_right = left;
        } else {
            parent->rb_left = left;
        }
    } else {
        root->rb_node = left;
    }
    
    rb_set_parent(node, left);
}

/*
 * rb_insert_color - Rebalance after insertion
 */
void rb_insert_color(struct rb_node *node, struct rb_root *root) {
    struct rb_node *parent, *gparent;
    
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);
        
        if (parent == gparent->rb_left) {
            struct rb_node *uncle = gparent->rb_right;
            
            if (uncle && rb_is_red(uncle)) {
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(parent, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }
            
            if (node == parent->rb_right) {
                rb_rotate_left(parent, root);
                struct rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }
            
            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            rb_rotate_right(gparent, root);
        } else {
            struct rb_node *uncle = gparent->rb_left;
            
            if (uncle && rb_is_red(uncle)) {
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(parent, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }
            
            if (node == parent->rb_left) {
                rb_rotate_right(parent, root);
                struct rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }
            
            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            rb_rotate_left(gparent, root);
        }
    }
    
    rb_set_color(root->rb_node, RB_BLACK);
}

/*
 * Rebalance after deletion
 */
static void rb_erase_color(struct rb_node *node, struct rb_node *parent,
                           struct rb_root *root) {
    struct rb_node *sibling;
    
    while ((!node || rb_is_black(node)) && node != root->rb_node) {
        if (parent->rb_left == node) {
            sibling = parent->rb_right;
            
            if (rb_is_red(sibling)) {
                rb_set_color(sibling, RB_BLACK);
                rb_set_color(parent, RB_RED);
                rb_rotate_left(parent, root);
                sibling = parent->rb_right;
            }
            
            if ((!sibling->rb_left || rb_is_black(sibling->rb_left)) &&
                (!sibling->rb_right || rb_is_black(sibling->rb_right))) {
                rb_set_color(sibling, RB_RED);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (!sibling->rb_right || rb_is_black(sibling->rb_right)) {
                    rb_set_color(sibling->rb_left, RB_BLACK);
                    rb_set_color(sibling, RB_RED);
                    rb_rotate_right(sibling, root);
                    sibling = parent->rb_right;
                }
                
                rb_set_color(sibling, rb_color(parent));
                rb_set_color(parent, RB_BLACK);
                rb_set_color(sibling->rb_right, RB_BLACK);
                rb_rotate_left(parent, root);
                node = root->rb_node;
                break;
            }
        } else {
            sibling = parent->rb_left;
            
            if (rb_is_red(sibling)) {
                rb_set_color(sibling, RB_BLACK);
                rb_set_color(parent, RB_RED);
                rb_rotate_right(parent, root);
                sibling = parent->rb_left;
            }
            
            if ((!sibling->rb_left || rb_is_black(sibling->rb_left)) &&
                (!sibling->rb_right || rb_is_black(sibling->rb_right))) {
                rb_set_color(sibling, RB_RED);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (!sibling->rb_left || rb_is_black(sibling->rb_left)) {
                    rb_set_color(sibling->rb_right, RB_BLACK);
                    rb_set_color(sibling, RB_RED);
                    rb_rotate_left(sibling, root);
                    sibling = parent->rb_left;
                }
                
                rb_set_color(sibling, rb_color(parent));
                rb_set_color(parent, RB_BLACK);
                rb_set_color(sibling->rb_left, RB_BLACK);
                rb_rotate_right(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }
    
    if (node) {
        rb_set_color(node, RB_BLACK);
    }
}

/*
 * rb_erase - Remove a node from the tree
 */
void rb_erase(struct rb_node *node, struct rb_root *root) {
    struct rb_node *child, *parent;
    int color;
    
    if (!node->rb_left) {
        child = node->rb_right;
    } else if (!node->rb_right) {
        child = node->rb_left;
    } else {
        struct rb_node *old = node;
        struct rb_node *left;
        
        node = node->rb_right;
        while ((left = node->rb_left)) {
            node = left;
        }
        
        if (rb_parent(old)) {
            if (rb_parent(old)->rb_left == old) {
                rb_parent(old)->rb_left = node;
            } else {
                rb_parent(old)->rb_right = node;
            }
        } else {
            root->rb_node = node;
        }
        
        child = node->rb_right;
        parent = rb_parent(node);
        color = rb_color(node);
        
        if (parent == old) {
            parent = node;
        } else {
            if (child) {
                rb_set_parent(child, parent);
            }
            parent->rb_left = child;
            
            node->rb_right = old->rb_right;
            rb_set_parent(old->rb_right, node);
        }
        
        node->__rb_parent_color = old->__rb_parent_color;
        node->rb_left = old->rb_left;
        rb_set_parent(old->rb_left, node);
        
        goto color;
    }
    
    parent = rb_parent(node);
    color = rb_color(node);
    
    if (child) {
        rb_set_parent(child, parent);
    }
    
    if (parent) {
        if (parent->rb_left == node) {
            parent->rb_left = child;
        } else {
            parent->rb_right = child;
        }
    } else {
        root->rb_node = child;
    }
    
color:
    if (color == RB_BLACK) {
        rb_erase_color(child, parent, root);
    }
}

/*
 * rb_first - Find the first (leftmost) node
 */
struct rb_node *rb_first(const struct rb_root *root) {
    struct rb_node *n = root->rb_node;
    
    if (!n) return NULL;
    
    while (n->rb_left) {
        n = n->rb_left;
    }
    
    return n;
}

/*
 * rb_last - Find the last (rightmost) node
 */
struct rb_node *rb_last(const struct rb_root *root) {
    struct rb_node *n = root->rb_node;
    
    if (!n) return NULL;
    
    while (n->rb_right) {
        n = n->rb_right;
    }
    
    return n;
}

/*
 * rb_next - Find the next node in order
 */
struct rb_node *rb_next(const struct rb_node *node) {
    struct rb_node *parent;
    
    if (!node) return NULL;
    
    if (node->rb_right) {
        node = node->rb_right;
        while (node->rb_left) {
            node = node->rb_left;
        }
        return (struct rb_node *)node;
    }
    
    while ((parent = rb_parent(node)) && node == parent->rb_right) {
        node = parent;
    }
    
    return parent;
}

/*
 * rb_prev - Find the previous node in order
 */
struct rb_node *rb_prev(const struct rb_node *node) {
    struct rb_node *parent;
    
    if (!node) return NULL;
    
    if (node->rb_left) {
        node = node->rb_left;
        while (node->rb_right) {
            node = node->rb_right;
        }
        return (struct rb_node *)node;
    }
    
    while ((parent = rb_parent(node)) && node == parent->rb_left) {
        node = parent;
    }
    
    return parent;
}