/*
   netrik -- The ANTRIK Internet Viewer
   Copyright (C) Olaf D. Buddenhagen AKA antrik, et al (see AUTHORS)
   Published under the GNU GPL; see LICENSE for details.
*/
/*
 * dump-tree.c -- this contains a function for dumping the syntax tree
 * generated by parse_syntax().
 *
 * (C) 2001 antrik
 *
 * It is fairly simple and used for debugging purposes, but could be easily
 * evolved into a useful HTML file structure dumping tool. That's why it has
 * a file on it's own.
 */
#include "syntax.h"

#include <stdio.h>

/* dump element hierarchy and (optionally) content from the syntax parse tree */
void dump_tree(syntax_tree, dump_content, elements_parsed)
struct Element	*syntax_tree;    /* top of syntax tree to dump */
int		dump_content;    /* whether to dump everything or only elements */
int		elements_parsed;    /* elements and attributes already transformed by parse_elements() */
{
   struct Element	*cur_el;    /* currently handled element node */
   int			depth=0;    /* current depth in tree */
   struct Element	*new_el;
   int			new_depth;
   int			i;

   /* all elements in syntax parse tree */
   cur_el=syntax_tree;
   do {    /* until cur_el->parent==NULL (wrapped back to global element) */

      /* dump content before current element */
      if(dump_content)
	 if(cur_el->content!=NULL)
	    printf("%s\n", cur_el->content);

      /* dump current element */
      for(i=0; i<depth; ++i)
	 printf("|");
      printf("_ <");
      if(elements_parsed) {
	 printf("%s", element_table[cur_el->name.type].name);
      } else {
	 if(cur_el->name.str!=NULL)
	    printf("%s", cur_el->name.str);
	 else
	    printf("*");
      }
      for(i=0; i<cur_el->attr_count; ++i)
	 if(elements_parsed) {
	    if(attr_table[cur_el->attr[i].name.type].numeric)
	       printf(" %s=\"%i\"", attr_table[cur_el->attr[i].name.type].name, cur_el->attr[i].value.num);
	    else
	       printf(" %s=\"%s\"", attr_table[cur_el->attr[i].name.type].name, cur_el->attr[i].value.str);
	 } else
	    printf(" %s=\"%s\"", cur_el->attr[i].name.str, cur_el->attr[i].value.str);
      printf(">\n");

      fflush(stdout);    /* pagers behave funny on segfault... */

      /* calculate new tree depth */
      new_depth=depth+1;    /* assume that list_next is our child */
      for(new_el=cur_el; new_el!=cur_el->list_next->parent; new_el=new_el->parent)    /* if "list_next" is not our child, ascend till we find real parent */
	 --new_depth;
      depth=new_depth;

      cur_el=cur_el->list_next;
   } while(cur_el->parent!=NULL);    /* until wrapped back to global tag (all tags processed) */

   printf("\n");
}
