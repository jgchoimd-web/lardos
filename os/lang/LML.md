# LML (Lardos Markup Language)

**LML** is an OS-specific markup format for configuration, UI definitions, and simple documents. Syntax is XML-like but minimal.

## Syntax

- **Elements**: `<tag>...</tag>` or `<tag/>`
- **Attributes**: `name="value"` (double-quoted)
- **Comments**: `<!-- text -->` (block), `;` or `#` to end of line (line)
- **Text content**: between tags (whitespace trimmed at boundaries)

### Entity escapes (in attribute values)

- `\"` → `"`
- `\n` → newline
- `\t` → tab
- `\&` → `&`

## Example: Config

```lml
<config>
  <setting name="boot_delay" value="3"/>
  <setting name="resolution" w="320" h="200"/>
</config>
```

## Example: UI layout

```lml
<screen id="main">
  <title>LardOS</title>
  <menu>
    <item action="demo">Run Demo</item>
    <item action="shell">Shell</item>
  </menu>
</screen>
```

## Example: Document

```lml
<doc>
  <title>Welcome</title>
  <p>Hello, world.</p>
</doc>
```

## C API

```c
#include "lml.h"

/* Callback-based (no allocation) */
int lml_parse(const char* src, lml_cb_fn cb, void* user);

/* Tree builder (use lml_tree_free when done) */
lml_node_t* root;
if (lml_parse_tree(src, &root) == 0) {
    lml_node_t* doc = root->nchild > 0 ? root->child[0] : 0;
    const char* id = lml_attr(doc, "id");
    lml_tree_free(root);
}
```

## File extension

Use `.lml` for LML source files.
