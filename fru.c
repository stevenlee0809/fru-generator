#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <endian.h>
#include <time.h>

#define FRU_TYPE_LENGTH_TYPE_CODE_SHIFT		0x06
#define FRU_TYPE_LENGTH_TYPE_CODE_LANGUAGE_CODE 0x03
#define FRU_TYPE_LENGTH_TYPE_CODE_BIN_CODE 0x00

#define FRU_SENTINEL_VALUE	0xC1
#define FRU_FORMAT_VERSION	0x01

#define FRU_COMMON_AREA_MAX_LENGTH	2048       // 256 * 8
#define FRU_COMMON_AREA_LENGTH_OFFSET	0x01

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

void fru_bin_append_byte(struct fru_bin *bin,uint8_t data)
{
	if(bin->length >= bin->size){
		size_t new_size = bin->size*2;
		_fru_bin_expand(bin,new_size);
	}

	bin->data[bin->length] = data;
	bin->length++;
}


void fru_bin_append_bytes(struct fru_bin *bin,const void *data,size_t len)
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

void fru_common_area_init_append(struct fru_bin *bin)
{
	fru_bin_append_byte(bin,FRU_FORMAT_VERSION);
	fru_bin_append_byte(bin,0);
}


void fru_common_area_final_append(struct fru_bin *bin)
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

void fru_board_area_append_mfg(struct fru_bin *bin,const char *time)
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

void fru_common_area_type_length_append_language_data(struct fru_bin *bin,
			const void *data,size_t len)
{
	uint8_t type_length = type_length_code(FRU_TYPE_LENGTH_TYPE_CODE_LANGUAGE_CODE,
					len);
	fru_bin_append_byte(bin,type_length);	
	fru_bin_append_bytes(bin,data,len);

}

void fru_common_area_type_length_append_bin_data(struct fru_bin *bin,
			const void *data,size_t len)
{
	uint8_t type_length = type_length_code(FRU_TYPE_LENGTH_TYPE_CODE_BIN_CODE,
					len);

	fru_bin_append_byte(bin,type_length);	
	fru_bin_append_bytes(bin,data,len);
}

void fru_chassis_info_append(struct fru_bin *bin,uint8_t type,const char *partnumber,
			const char *serialnumber)
{
	fru_common_area_init_append(bin);

	fru_bin_append_byte(bin,type);
	fru_common_area_type_length_append_language_data(
			bin,partnumber,strlen(partnumber));
	fru_common_area_type_length_append_language_data(
			bin,serialnumber,strlen(serialnumber));

	fru_common_area_final_append(bin);
}


int main()
{
	struct fru_bin *bin = fru_bin_create(100);

	fru_chassis_info_append(bin,1,"inspur","ON5263m5");

	fru_bin_debug(bin);
}
