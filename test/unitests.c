#include <fdp.h>
#include <stdio.h>
                                                                                                     
// TODO(mfd) : add the testing functions and call them from main                                     
                                                                                                     
void test_is_valid_nvme_device() {                                                                   
  const char *tests[] = {"/dev/nvme0n1", "/dev/nvme1n1p2", "/dev/sda1",                              
                         "/dev/nvme0n", "/dev/nvme0n1p"};                                            
                                                                                                     
  for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {                                  
    printf("%s => %s\n", tests[i],                                                                   
           is_valid_nvme_device(tests[i]) ? "valid" : "invalid");                                    
    // open_nvme_char_file(tests[i]);                                                                
  }                                                                                                  
}                                                                                                    
                                                                                                     
int main(int argc, char *argv[]) {                                                                   
    test_is_valid_nvme_device();                                                                     
    return 0;                                                                                        
} 
