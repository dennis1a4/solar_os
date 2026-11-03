#include "solar_os_epub.h"
#include "solar_os_zip.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void vTaskDelay(TickType_t ticks){(void)ticks;}
esp_err_t solar_os_storage_path_mount_point(const char *path,char *out,size_t size){(void)path;if(size<2)return ESP_ERR_INVALID_SIZE;strcpy(out,"/");return ESP_OK;}
int main(int argc,char **argv){assert(argc==2);solar_os_epub_book_t *book=NULL;assert(solar_os_epub_open(argv[1],&book)==ESP_OK);assert(solar_os_epub_spine_count(book)==2);assert(!strcmp(solar_os_epub_title(book),"Teensy Reader Test"));solar_os_doc_t doc;solar_os_doc_init(&doc);
 assert(solar_os_epub_load_spine_doc(book,0,&doc)==ESP_OK);assert(strstr(doc.source,"First chapter")&&strstr(doc.source,"Needle"));assert(strstr(doc.source,"images/picture.png"));
 uint8_t *data=NULL;size_t n=0;assert(solar_os_epub_asset_read(book,doc.path,"images/picture.png",&data,&n)==ESP_OK);assert(n>8&&!memcmp(data,"\x89PNG\r\n\x1a\n",8));solar_os_epub_asset_release(book,data);
 assert(solar_os_epub_asset_read(book,doc.path,"https://invalid.example/image.png",&data,&n)!=ESP_OK);
 assert(solar_os_epub_load_spine_doc(book,1,&doc)==ESP_OK);assert(strstr(doc.source,"Second chapter"));assert(solar_os_epub_load_spine_doc(book,2,&doc)!=ESP_OK);solar_os_doc_free(&doc);solar_os_epub_close(book);
 assert(solar_os_epub_open("/no-such-epub-file",&book)!=ESP_OK&&book==NULL);puts("EPUB: deflated spine, XHTML conversion, relative PNG assets, chapter bounds and cleanup passed");}
