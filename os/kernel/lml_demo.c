/*
 * LML demo: parse sample markup and show result.
 */
#include "lml.h"
#include "lml_demo.h"
#include "string.h"
#include <stdint.h>

static const char LML_SAMPLE[] =
    "<doc>"
    "<title>LML</title>"
    "<p>Hello, markup.</p>"
    "</doc>";

static void append_str(char* buf, uint32_t* len, uint32_t cap, const char* s)
{
    while (*len + 1 < cap && *s) {
        buf[(*len)++] = *s++;
    }
}

int lml_demo(char* out, uint32_t out_cap)
{
    if (!out || out_cap < 32) return -1;
    out[0] = '\0';

    lml_node_t* root = 0;
    if (lml_parse_tree(LML_SAMPLE, &root) != 0) return -2;
    if (!root || root->nchild == 0) {
        if (root) lml_tree_free(root);
        return -3;
    }

    lml_node_t* doc = root->child[0];
    const char* title = 0;
    const char* ptext = 0;
    for (uint32_t i = 0; i < doc->nchild; i++) {
        lml_node_t* ch = doc->child[i];
        if (ch->tag && strcmp(ch->tag, "title") == 0 && ch->text) title = ch->text;
        if (ch->tag && strcmp(ch->tag, "p") == 0 && ch->text) ptext = ch->text;
    }

    uint32_t n = 0;
    append_str(out, &n, out_cap, "LML: ");
    if (title) append_str(out, &n, out_cap, title);
    if (title && ptext) append_str(out, &n, out_cap, " - ");
    if (ptext) append_str(out, &n, out_cap, ptext);
    if (!title && !ptext) append_str(out, &n, out_cap, "OK");
    out[n] = '\0';

    lml_tree_free(root);
    return 0;
}
