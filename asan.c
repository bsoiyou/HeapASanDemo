// gcc asan.c -o asan -no-pie
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include <stdbool.h>

#define SHADOW_SIZE 496

// 섀도우 메모리
char shadow_memory[SHADOW_SIZE];

// 섀도우 오프셋 
int shadow_offset = 0;

// 섀도우 메모리 출력 함수 
void print_shadow_memory(){
  printf("---------------------- Shadow Memory ----------------------\n");
  // 섀도우 메모리의 값을 hexdump 형식으로 출력
  for (size_t addr = 0; addr < SHADOW_SIZE; addr += 16) {
      // 각 줄에 16바이트씩 출력
      printf("%p: ", (void *)(shadow_memory + addr));
      for (size_t i = 0; i < 16 && addr + i < SHADOW_SIZE; i++) {
          // 16진수로 값 출력
          printf("%02x ", (unsigned char)shadow_memory[addr + i]);  
      }
      printf("\n");
  }
  printf("-----------------------------------------------------------\n");
}

// 섀도우 메모리 세팅 함수 
// 1 바이트씩 값 설정 
int set_shadow_value(size_t base, char * shadow, size_t size, char state){
  int i;
  for(i=base; i < size; i++){
    shadow[i] = state;
  }
  return i;
}

// asan malloc 
void * asan_malloc(size_t requested_size, int offset){
    void *ptr = malloc(requested_size);  // 메모리 할당

    if (ptr == NULL) {
        perror("malloc failed");
        return NULL;
    }

    // 실제 할당된 청크 크기 확인 (헤더 제외)
    size_t actual_size = malloc_usable_size(ptr);

    printf("Requested size: %zu bytes\n", requested_size);
    printf("Usable size (with alignment): %zu bytes\n", actual_size);


    // shadow_memory 시작 주소에 offset을 더해서 현재 청크에 매핑되는 섀도우 메모리 주소 계산 
    char * shadow = shadow_memory + offset;

    int i;

    // heap left redzone 설정
    i = set_shadow_value(0, shadow, 16, 0xfa);

    // 할당 영역 설정
    i = set_shadow_value(i, shadow, 16 + requested_size, 0x00);

    // heap right redzone 설정
    i = set_shadow_value(i, shadow, 16 + actual_size, 0xfb);

    if(offset == 0){
      shadow_offset = actual_size + 16;
    }
    
    return ptr;
}

// asan free
void asan_free(void * ptr, int offset){
  // 섀도우 메모리 시작 주소에 offset을 더해서 현재 청크에 매핑되는 섀도우 메모리 주소 계산 
  char * shadow = shadow_memory + offset;

  // 실제 할당된 청크 크기 확인
  size_t actual_size = malloc_usable_size(ptr);

  // freed heap redzone 설정
  int i = set_shadow_value(0, shadow, actual_size + 16, 0xfd);

  // free
  free(ptr);

  // free 주소 출력 
  printf("Address %p is freed\n", ptr);
}

// 섀도우 메모리 검사 함수
void check_access(void* addr, size_t size, int offset) {
    // 헤더 건너뛰기 
    offset += 16;  
    uintptr_t base = (uintptr_t)addr;
    
    for (size_t i = 0; i < size; i++) {
        // 0x00이 아니면 에러 
        if (shadow_memory[offset + i] != 0x00) {
            printf("\n***********************************************************\n");
            fprintf(stderr, "Memory access error at %p\n", (void*)(base + i));
            fprintf(stderr, "Memory access error at Shadow Memory %p: [%02x]\n", (void*)(shadow_memory + offset + i), (unsigned char)shadow_memory[offset + i]);
            printf("***********************************************************\n\n");
            abort();
        }
    }
}

// asan memcpy
void asan_memcpy(void* dst, void* src, size_t size) {
    // src over-read check
    check_access(src, size, shadow_offset);
    // dst over-write check 
    check_access(dst, size, 0);
    
    // 체크 후 memcpy 호출 
    memcpy(dst, src, size);
}

// 메모리 값을 16진수로 출력하는 함수
void print_memory(void* ptr, size_t size, const char* label) {
    unsigned char* byte_ptr = (unsigned char*)ptr;
    printf("Memory content of %s:\n", label);
    for (size_t i = 0; i < size; i += 16) {
        printf("%p: ", byte_ptr + i);
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            printf("%02x ", byte_ptr[i + j]);
        }
        printf("\n");
    }
}

int main() {
    // 임의의 음수값으로 섀도우 메모리 초기화 
    memset(shadow_memory, 0xff, SHADOW_SIZE);

    size_t requested_size1;  // malloc size 1
    size_t requested_size2;  // malloc size 2 
    size_t requested_size; // memcpy size
    
    // 1. 첫 번째 메모리 할당
    printf("Enter size for first allocation (10-100): ");
    scanf("%zu", &requested_size1);
    // error
    if(requested_size1 > 100 || requested_size1 < 10){
      printf("Please enter 10-100 value\n");
      return 0;
    }
    void *ptr = asan_malloc(requested_size1, 0);
    if (!ptr) return 1;


    // 2. 두 번째 메모리 할당
    printf("Enter size for second allocation (10-100): ");
    scanf("%zu", &requested_size2);
    // error
    if(requested_size2 > 100 || requested_size2 < 10){
      printf("Please enter 10-100 value\n");
      return 0;
    }
    void *ptr2 = asan_malloc(requested_size2, shadow_offset);
    if (!ptr2) return 1;


    // 섀도우 메모리 출력 
    print_shadow_memory();
    

    // ptr2의 값을 모두 'a'로 세팅 
    printf("Set ptr2 memory to 'a' by %ld byte\n", requested_size2);
    memset(ptr2, 'a', requested_size2);
    

    // 각각 메모리 주소 출력
    printf("-----------------------------------------------------------\n");
    printf("Shadow Memory:                %p\n", &shadow_memory);
    printf("ptr Memory  (1st allocation):  %p\n", ptr);
    printf("ptr2 Memory (2nd allocation): %p\n", ptr2);
    printf("-----------------------------------------------------------\n");


    // Heap Out of bounds
    // 사용자가 원하는 크기로 memcpy 실행 
    printf("Enter size for memcpy from ptr2 to ptr: ");
    scanf("%zu", &requested_size);
    asan_memcpy(ptr, ptr2, requested_size);  

    // memcpy 후 ptr 메모리 내용 출력
    print_memory(ptr, requested_size1, "ptr");


    // 메모리 해제
    printf("-----------------------------------------------------------\n");
    printf("Free ptr and ptr2 :) \n");
    asan_free(ptr, 0);
    asan_free(ptr2, shadow_offset);


    // 섀도우 메모리 출력 
    print_shadow_memory();


    // Use After Free 
    printf("Checking access to freed memory ptr data by 1 byte ... \n");
    check_access(ptr, 1, 0);


    return 0;
}
