#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <endian.h>
#include <time.h>

#include "fru.h"

#define FRU_TYPE_LENGTH_TYPE_CODE_SHIFT		0x06
#define FRU_TYPE_LENGTH_TYPE_CODE_LANGUAGE_CODE 0x03
#define FRU_TYPE_LENGTH_TYPE_CODE_BIN_CODE 0x00

#define FRU_SENTINEL_VALUE	0xC1
#define FRU_FORMAT_VERSION	0x01

#define FRU_COMMON_AREA_MAX_LENGTH	2048       // 256 * 8
#define FRU_COMMON_AREA_LENGTH_OFFSET	0x01
#define FRU_AREA_TYPE_LENGTH_FIELD_MAX  512

static uint8_t crc_calculate(const uint8_t *data,size_t len)
{
	uint8_t sum = 0;
	size_t i;
	for(i = 0;i < len;i++)
		sum += data[i];

	return (-sum);
}


struct fru_common_hdr
{
	uint8_t fmtver;
	uint8_t internal;
	uint8_t chassis;
	uint8_t board;
	uint8_t product;
	uint8_t multirec;
	uint8_t pad;
	uint8_t crc;
} __attribute__((packed));

struct fru_bin{
	uint8_t *data;
	size_t size;
	size_t length;
};


struct fru_bin *fru_bin_create(size_t size)
{
	struct fru_bin *bin = malloc(sizeof(*bin));
	assert(bin != NULL);
	bin->data = malloc(size);
	assert(bin->data != NULL);
	bin->size = size;
	bin->length = 0;

	return bin;
}

void fru_bin_release(struct fru_bin *bin)
{
	free(bin->data);
	free(bin);	
}

static void _fru_bin_expand(struct fru_bin *bin,size_t new_size)
{
	bin->data = realloc(bin->data,new_size);
	assert(bin->data != NULL);
	bin->size = new_size;
}

static void fru_bin_append_byte(struct fru_bin *bin,uint8_t data)
{
	if(bin->length >= bin->size){
		size_t new_size = bin->size*2;
		_fru_bin_expand(bin,new_size);
	}

	bin->data[bin->length] = data;
	bin->length++;
}


static void fru_bin_append_bytes(struct fru_bin *bin,const void *data,size_t len)
{
	size_t length_need = bin->length + len;
	if( length_need >= bin->size ){
		size_t new_size = length_need*2;
		_fru_bin_expand(bin,new_size);
	}
	
	memcpy(bin->data+bin->length,data,len);
	bin->length += len;

}

void fru_bin_debug(struct fru_bin *bin)
{
	printf("length=%zu,size=%zu\ndata:",bin->length,bin->size);
	size_t i;
	uint8_t sum = 0;
	for(i=0;i<bin->length;i++){
		if(i%16 == 0)
			printf("\n\t");
		printf("0x%.2x ",bin->data[i]);
		sum += bin->data[i];
	}
	printf("\nsum=0x%.2x\n",sum);
}

static void fru_common_area_init_append(struct fru_bin *bin)
{
	fru_bin_append_byte(bin,FRU_FORMAT_VERSION);
	fru_bin_append_byte(bin,0);
}


static void fru_common_area_final_append(struct fru_bin *bin)
{
	fru_bin_append_byte(bin,FRU_SENTINEL_VALUE);

	int m = (bin->length+1) & 7;
	if(m != 0){
		int remain_length = 8-m;
		int i;
		for(i=0;i < remain_length;i++)
			fru_bin_append_byte(bin,0);
	}
	
	bin->data[FRU_COMMON_AREA_LENGTH_OFFSET] = (bin->length+1)>>3;
	uint8_t crc = crc_calculate(bin->data,bin->length);
	fru_bin_append_byte(bin,crc);
	
}

static void fru_board_area_append_mfg(struct fru_bin *bin,const char *time)
{
	struct tm tm;
	memset(&tm, 0, sizeof(struct tm));
	strptime(time,"%Y-%m-%d %H:%M:%S",&tm);
	struct tm tm_96;
	memset(&tm_96, 0, sizeof(struct tm));
	strptime("1996-1-1 00:00:00","%Y-%m-%d %H:%M:%S",&tm_96);
	
	time_t sdiff = mktime(&tm) - mktime(&tm_96);
	uint32_t mdiff = htole32(sdiff/60);

	fru_bin_append_bytes(bin,&mdiff,3);

}


static uint8_t type_length_code(uint8_t type,size_t length)
{
	length = length & ( (1 << FRU_TYPE_LENGTH_TYPE_CODE_SHIFT) - 1 );
	type = type << FRU_TYPE_LENGTH_TYPE_CODE_SHIFT;
	return length|type;
}

static void fru_area_field_init_by_string(struct fru_bin *field,const char *string)
{
        field->length = 0;

        uint8_t len = strlen(string);
        uint8_t type_length = type_length_code(FRU_TYPE_LENGTH_TYPE_CODE_LANGUAGE_CODE,len);
        fru_bin_append_byte(field,type_length);
        fru_bin_append_bytes(field,string,len);
}

struct fru_bin *fru_area_field_create_by_string(const char *string)
{
        struct fru_bin *field = fru_bin_create(64);
        fru_area_field_init_by_string(field,string);

        return field;
}


static void fru_common_area_field_append(struct fru_bin *bin,struct fru_bin *field)
{
        fru_bin_append_bytes(bin,field->data,field->length);
}

static void fru_common_area_custom_field_append(struct fru_bin *bin,struct fru_bin **custom_field)
{
        int i;
        for(i=0;i<OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX;i++){
                if(custom_field[i] == NULL)
                        return;
                fru_common_area_field_append(bin,custom_field[i]);
        }
        
}


#define FRU_COMMON_AREA_FIELD_APPEND(bin,field)         \
        do {                                            \
           if(field == NULL) {                          \
                   fru_common_area_final_append(bin);   \
                   return;                              \
           }                                            \
           fru_common_area_field_append(bin,field);     \
        } while(0)                                      

void fru_chassis_info_append(struct fru_bin *bin,struct chassis_info *chassis)
{

	fru_common_area_init_append(bin);
        
	fru_bin_append_byte(bin,chassis->type);
        FRU_COMMON_AREA_FIELD_APPEND(bin,chassis->part_number);
        FRU_COMMON_AREA_FIELD_APPEND(bin,chassis->serial_number);

        fru_common_area_custom_field_append(bin,chassis->custom_field);
        fru_common_area_final_append(bin);
}

void fru_board_info_append(struct fru_bin *bin,struct board_info *board)
{
	fru_common_area_init_append(bin);
	fru_bin_append_byte(bin,board->language_code);
        fru_board_area_append_mfg(bin,board->time);

        FRU_COMMON_AREA_FIELD_APPEND(bin,board->manufacturer);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,board->product_name);
        FRU_COMMON_AREA_FIELD_APPEND(bin,board->serial_number);
        FRU_COMMON_AREA_FIELD_APPEND(bin,board->part_number);
        FRU_COMMON_AREA_FIELD_APPEND(bin,board->fru_file_id);

        fru_common_area_custom_field_append(bin,board->custom_field);

        fru_common_area_final_append(bin);

}

void fru_product_info_append(struct fru_bin *bin,struct product_info *product)
{
	fru_common_area_init_append(bin);
	fru_bin_append_byte(bin,product->language_code);
        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->manufacturer);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->product_name);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->part_number);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->version);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->serial_number);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->asset_tag);        
        FRU_COMMON_AREA_FIELD_APPEND(bin,product->fru_file_id);        

        fru_common_area_custom_field_append(bin,product->custom_field);

        fru_common_area_final_append(bin);

}

int main()
{
       struct chassis_info chassis;
       chassis.type = 0; 

       chassis.serial_number = fru_area_field_create_by_string("inspur");
       chassis.part_number= fru_area_field_create_by_string("on5263m5");

        struct fru_bin *bin = fru_bin_create(100);
       fru_chassis_info_append(bin,&chassis);
       fru_bin_debug(bin);
}
