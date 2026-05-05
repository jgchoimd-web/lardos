#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * LML (Lardos Markup Language) — OS-specific markup for UI, config, documents.
 *
 * Syntax (XML-like, minimal):
 *   <tag>...</tag>   element with content
 *   <tag/>           empty element
 *   <tag attr="val"/> attributes (double-quoted)
 *   <!-- comment --> comments
 *
 * Callback-based parser: no allocation for tree (or optional tree builder).
 */

typedef struct lml_parser lml_parser_t;

/* Event types for callback */
typedef enum {
    LML_OPEN_TAG,    /* element opened: name, attributes */
    LML_CLOSE_TAG,   /* element closed: name */
    LML_TEXT,        /* text content between tags */
    LML_ATTR         /* single attribute (during OPEN_TAG) */
} lml_event_t;

/* Callback: (event, name, value, user). For OPEN_TAG name=tag, value=0.
 * For ATTR name=attr, value=attr_value. For TEXT name=0, value=text. */
typedef int (*lml_cb_fn)(lml_event_t ev, const char* name, const char* value, void* user);

/* Parse LML source. Returns 0 on success, non-zero on error.
 * Calls cb for each event. Parser does not allocate except for internal buffers. */
int lml_parse(const char* src, lml_cb_fn cb, void* user);

/* Optional: build a simple tree. lml_node_t has tag, attrs, children, text. */
typedef struct lml_node lml_node_t;

struct lml_node {
    char* tag;           /* tag name (owned) */
    char* text;         /* text content if leaf (owned), 0 if not */
    lml_node_t** child; /* child elements */
    uint32_t nchild;
    char** attr_names;  /* attribute names */
    char** attr_vals;   /* attribute values */
    uint32_t nattr;
};

/* Parse and build tree. Caller must lml_tree_free(root).
 * Returns 0 and sets *out, or non-zero on error. */
int lml_parse_tree(const char* src, lml_node_t** out);

void lml_tree_free(lml_node_t* n);

/* Get attribute value by name. Returns 0 if not found. */
const char* lml_attr(const lml_node_t* n, const char* name);
