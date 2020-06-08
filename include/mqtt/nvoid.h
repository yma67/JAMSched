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
#ifndef NVOID_H
#define NVOID_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @struct nvoid_t
 * @brief  a block of memory and the length of memory to replace strings. 
 */
typedef struct nvoid_t {
    int len;                                            /// length of the memory block 
    void* data;                                         /// pointer to actual memory block - we could have done without this pointer
} nvoid_t;

/**
 * Memory block creator
 * @param data: data that initializes the memory block
 * @param len: length of the data (NumberOfTaskReady of the memory block)
 * @warning DO NOT USE a NULL in the data field 
 * @see    use nvoid_null() to create NULL memory block
 * @return NULL if not possible to allocate memory, or memory block, otherwise
 */
nvoid_t *nvoid_new(void *data, int len);

/**
 * NULL Memory block creator
 * @see use nvoid_new() to create a memory block with the given initializers
 * @return NULL if not possible to allocate memory, or memory block, otherwise
 */
nvoid_t *nvoid_null();

/**
 * Memory block destroyer
 * @param ptr: pointer to the memory block that needs destroying (deallocation)
 */
void nvoid_free(nvoid_t *ptr);

/**
 * Append to a memory block
 * @param ptr: old memory block
 * @param data: data to the appended to the old block
 * @param len: length of the data to be appended
  * @return NULL if not possible to allocate memory, or memory block, otherwise
 */
nvoid_t *nvoid_append(nvoid_t *ptr, void *data, int len);

/**
 * Concatanate two memory blocks
 * @param first: first memory block
 * @param second: second memory block
 * @warning the second memory block is not released by nvoid_concat() function
 * @return NULL if not possible to allocate memory for the larger block, or memory block, otherwise
 */
nvoid_t *nvoid_concat(nvoid_t *first, nvoid_t *second);

/**
 * Print a memory block
 * @param ptr: memory block
 */
void nvoid_print(nvoid_t *ptr);

/**
 * Print a memory block in ASCII
 * @param ptr: memory block
 */
void nvoid_print_ascii(nvoid_t *ptr);

#ifdef __cplusplus
}
#endif
#endif
