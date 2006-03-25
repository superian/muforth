/*
 * $Id$
 *
 * This file is part of muforth.
 *
 * Copyright (c) 1997-2006 David Frech. All rights reserved, and all wrongs
 * reversed.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * or see the file LICENSE in the top directory of this distribution.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* dictionary */

#include "muforth.h"
#include <stdio.h>

struct dict_entry
{
    /* char name[];           */
    /* char padding[];        */
    /* unsigned char length;  */
    struct dict_entry *link;  /* links point to link field */
    pw code;
    uint8 body[];
};

/* the forth and compiler "vocabulary" chains */
static struct dict_entry *forth_chain = NULL;
static struct dict_entry *compiler_chain = NULL;

/* current chain to compile into */
static struct dict_entry **current_chain = &forth_chain;

static xtk mu_name_hook = &p_mu_nope;  /* called when a name is created */

/* bogus C-style dictionary init */
struct inm          /* "initial name" */
{
    char *name;
    pw   code;
};

struct inm initial_forth[] = {
#include "forth_chain.h"
    { NULL, NULL }
};

struct inm initial_compiler[] = {
#include "compiler_chain.h"
    { NULL, NULL }
};

void mu_push_current()
{
    PUSH(&current_chain);
}

void mu_push_forth_chain()
{
    PUSH(&forth_chain);
}

void mu_push_compiler_chain()
{
    PUSH(&compiler_chain);
}

/*
 * 2004-apr-01. After giving a talk on muforth to SVFIG, and in particular
 * after Randy asked me some pointed questions, I decided that find should
 * have positive logic after all. I have renamed -"find to find to indicate
 * this change.
 */
/* find  ( a u chain - a u 0 | code -1) */
void mu_find()
{
    char *token = (char *) TRD;
    cell length = SND;
    struct dict_entry *pde = (struct dict_entry *) T;
    char *pnm;
    cell dict_length;

    while ((pde = pde->link) != NULL)
    {
        pnm = (uint8 *)&pde->link;
        dict_length = pnm[-1];
        if (dict_length != length) continue;
        if (memcmp(pnm - ALIGNED(dict_length + 1), token, length) != 0)
            continue;

        /* found: drop token, push code address and true flag */
        NIP;
        SND = (cell) &pde->code;  /* XXX: should be body? */
        T = -1;
        return;
    }
    /* not found: leave token, push false */
    T = 0;
}

static void compile_dict_entry(
    struct dict_entry **ppde, char *name, int length)
{
    char *pnm;
    struct dict_entry *pde;

    pnm = (char *)ALIGNED(pcd);

    /* Lay down the name and padding. Leave room for count byte. */
    memcpy(pnm, name, length);                 /* copy name string */
    pnm = (char *)ALIGNED(pnm + length + 1);   /* "allot" it, with count */
    pnm[-1] = length;                          /* store count */

    pde = (struct dict_entry *)pnm;
    pde->link = *ppde;      /* compile link to last */
    *ppde = pde;        /* link onto front of chain */

    /* Allot entry; now pointing to code field. */
    pcd = (cell *)&pde->code;

#if defined(BEING_DEFINED)
    printf("  %p %.*s\n", pcd, length, name);
#endif
}

/* Called from Forth. Only creates a name; does NOT set the code field! */
void mu_compile_name()
{
    compile_dict_entry(current_chain, (char *)SND, T);
    DROP2;
}

void mu_new()
{
    mu_token();
    EXEC(mu_name_hook);
    mu_compile_name();
}

void mu_push_tick_name_hook()
{
    PUSH(&mu_name_hook);
}

static void init_chain(struct dict_entry **pchain, struct inm *pinm)
{
    for (; pinm->name != NULL; pinm++)
    {
        compile_dict_entry(pchain, pinm->name, strlen(pinm->name));
        *pcd++ = (cell)pinm->code;  /* set code pointer */
        }
}

void init_dict()
{
    init_chain(&forth_chain, initial_forth);
    init_chain(&compiler_chain, initial_compiler);
}

static void print_name(cell *pcode)
{
    char *pnm = (char *)(pcode - 1);  /* back up over link field */
    int length = pnm[-1];

    printf("%.*s", length, pnm - ALIGNED(length + 1));
}


/* returns termination flag */
static int print_w(xtk *ip)
{
    xtk w = *ip;

    if ((w > (xtk)pcd0) && (w < (xtk)pcd) && (*w < (pw)pcd0))
    {
        printf("\n%p: ", ip);
        print_name((cell *)w);
    }
    else
    {
        printf(" %x", (cell)w);
    }
    return w == &p_mu_exit;
}

void dis(xtk *ip)
{
    if (*(pw *)ip == &mu_do_colon)
        print_name((cell *)ip++);

    for (;;)
    {
        if (print_w(ip++)) break;
    }
    printf("\n");
}
    
/* 
   We're going to take some pointers from Martin Tracy and zenFORTH.
   `last-link' is a 2variable that stores (link, chain) of the last
   named word defined.  We don't actually link it into the dictionary
   until we think it's safe to do so, thereby obviating the need for
   `smudge'.

   2variable last-link  ( &link &chain)
   variable last-code   ( pointer to last-compiled code field)
   variable last-word   ( last compiled word)

   : show     last-link 2@  ( &link chain)  !   ; 
   : use      ( code -)  last-code @ !  ;
   : patch    pop @  use   ; 

   : ?unique  ( a u - a u)
   2dup current @  ?unique-hook  find  if
   drop  2dup  fd-out @ push  >stderr  type  ."  again.  "  pop writes 
   then   2drop ;

   : code,   0 , ( lex)  here last-code !  0 , ( code)  ;
   : token,  ( - 'link)  token  ?unique  here  scrabble>  allot  ;
   : link,   ( here)  current @  dup @ ,  last-link 2!  ;
   : head,   token,  link,  ;

   : name        head,  code,  ;
   : noname   0 align,  code,  ;

   ( Dictionary structure)
   : link>name   1- ( char-)  dup c@  swap over -  swap  ;
   : >link  ( body - link)  cell- cell- cell- ;
   : link>  ( link - body)  cell+ cell+ cell+ ;
   : >name  ( body - a u)   >link  link>name  ;
   : >lex   ( body - lex)   cell- cell-  ;
   : >code  ( body - code)  cell-  ;
*/

#ifdef DEBUG_FOO
/*
 * daf: stripped the mu_ off the names of these routines, since they conform
 * to C stack API rather than muforth stack API.
 */

/*
 * find_pde_code_range()
 *
 * This routine will locate the supplied address to a specific
 * word within a given chain.  The caller (mu_fund_pde_by_addr()) will
 * determine if a word is in the compiler chain or the forth chain.
 * It does this by determining which has the lowest gap between
 * addr and the start of the function of each chain in which
 * the addr matches.
 */
static struct dict_entry *find_pde_code_range(struct dict_entry *chain, cell addr)
{
    struct dict_entry *pde, *best;
    u_int32_t closest, delta;

    /*
     * First pass: Check for direct hit
     */
    pde = chain;
    best = NULL;
    closest = 0x7FFF;
    do {
        if (addr == (cell) pde->code)
            return pde;
        delta = addr - (cell) pde->code;
        if (delta < closest) {
            best = pde;
            closest = delta;
        }
        pde = pde->link;
    } while (pde);

    if (closest != 0x7FFF)
        return best;

    return NULL;
}

/*
 * find_pde_by_addr()
 *
 * This routine calls mu_find_pde_code_range() to identify a possible
 * pde in both chains.  If nothing is found, it returns NULL.  Else,
 * it returns the pde for which the code segment is most likely the
 * the correct code segment for this address.  The heuristic for this
 * is, if the addr does not match the start address of either pde,
 * which pde has the smallest difference between the supplied addr
 * and the start of the code for that pde.
 */
static struct dict_entry *find_pde_by_addr(cell addr)
{
    struct dict_entry *pde_compiler, *pde_forth, *pde;
    u_int32_t comp_code, forth_code;

    pde_compiler = find_pde_code_range(compiler_chain, addr);
    pde_forth = find_pde_code_range(forth_chain, addr);

    if (!pde_compiler && !pde_forth)
        return NULL;

    if (pde_compiler)
        comp_code = (cell) pde_compiler->code;
    else
        comp_code = 0;

    if (pde_forth)
        forth_code = (cell) pde_forth->code;
    else
        forth_code = 0;

    if (comp_code  == addr)
        pde = pde_compiler;
    else if (forth_code == addr)
        pde = pde_forth;
    else {
        /*
         * This is the heuristic: we check to see which code
         * segment start is the closet to the supplied addr.
         */
        if ((addr - comp_code) < (addr - forth_code))
            pde = pde_compiler;
        else
            pde = pde_forth;
    }

    return pde;
}

/*
 * snprint_func_name()
 *
 * Given an address, this routine will print the name of the muforth word
 * which is associated with that address.  Also, it will print an offset
 * value if the address isn't the start of the code segment.
 *
 * It returns the base address of the muforth word.
 */
cell snprint_func_name(char *line, int size, cell addr)
{
    struct dict_entry *pde;
    cell code;
    int idx;

    pde = find_pde_by_addr(addr);

    if (!pde) {
        snprintf(line, size, "Unknown function: address %p", (cell *) addr);
        return addr;
    }

    code = (cell) pde->code;

    idx = snprintf(line, size, "%.*s", pde->length, pde->name);
    if (code != addr) {
        snprintf(line +idx, size -idx, " + %d", addr - code);
    }

    return code;
}
#endif /* DEBUG */
