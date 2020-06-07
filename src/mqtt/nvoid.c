/**
 * @file        nvoid.h
 * @brief       Memory block management functions
 * @details     Can store a memory segment pointed to by a void pointer in this block, binary data is accepted here. 
 * @author      Muthucumaru Maheswaran, Yuxiang Ma
 * @copyright 
 *              Copyright 2020 Muthucumaru Maheswaran, Yuxiang Ma
 * 
 *              Licensed under the Apache License, Version 2.0 (the "License");
 *              you may not use this file except in compliance with the License.
 *              You may obtain a copy of the License at
 * 
 *                  http://www.apache.org/licenses/LICENSE-2.0
 * 
 *              Unless required by applicable law or agreed to in writing, software
 *              distributed under the License is distributed on an "AS IS" BASIS,
 *              WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *              See the License for the specific language governing permissions and
 *              limitations under the License.
 */
#include "nvoid.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

nvoid_t *nvoid_new(void *data, int len)
{
    nvoid_t *nv = (nvoid_t *)calloc(1, sizeof(nvoid_t) + len);
    assert(nv != NULL);
    nv->len = len;
    nv->data = (void *)nv + sizeof(nvoid_t);
    memcpy(nv->data, data, len);
    return nv;
}

nvoid_t *nvoid_null()
{
    nvoid_t *nv = (nvoid_t *)calloc(1, sizeof(nvoid_t));
    return nv;
}

inline void nvoid_free(nvoid_t *x) {
    free(x);
}

nvoid_t *nvoid_append(nvoid_t *n, void *data, int len)
{
    nvoid_t *nd = (void *)realloc(n, sizeof(nvoid_t) + n->len + len);
    assert(nd != NULL);
    void *ptr = (void *)nd + sizeof(nvoid_t) + nd->len;
    memcpy(ptr, data, len);
    nd->len = nd->len + len;
    return nd;
}

nvoid_t *nvoid_concat(nvoid_t *f, nvoid_t *s)
{
    nvoid_t *nd = nvoid_append(f, s->data, s->len);
    return nd;
}


#define PRINT_WIDTH                             80
#define GROUP_WIDTH                             4

void nvoid_print(nvoid_t *n)
{
    int i;
    printf("\n");
    for (i = 0; i < n->len; i++)
    {
        if (i % PRINT_WIDTH == 0)
            printf("\n");
        if (i % GROUP_WIDTH == 0)
            printf(" ");
        printf("%x", *(int8_t *)&n->data[i]);
    }
    printf("\n");
}


void nvoid_print_ascii(nvoid_t *n)
{
    int i;
    printf("\n");
    for (i = 0; i < n->len; i++)
    {
        if (i % PRINT_WIDTH == 0)
            printf("\n");
        if (i % GROUP_WIDTH == 0)
            printf(" ");
        printf("%c", *(int8_t *)&n->data[i]);
    }
    printf("\n");
}
