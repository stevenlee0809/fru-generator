#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "fru.h"

#define ERROR_FIELD(area, field)                                               \
	do {                                                                   \
		fprintf(stderr,                                                \
			area " " field " field error,check the json file!\n"); \
		return -1;                                                     \
	} while (0)

static int custom_field_init_by_json(const char **field, cJSON *json) {
	cJSON *array = cJSON_GetObjectItem(json, "custom_field");
	int array_size = cJSON_GetArraySize(array);
	array_size = array_size < OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX
			 ? array_size
			 : OPENBMC_VPD_KEY_CUSTOM_FIELDS_MAX;
	int i;

	for (i = 0; i < cJSON_GetArraySize(array); i++) {
		cJSON *item = cJSON_GetArrayItem(array, i);
		field[i] = cJSON_GetStringValue(item);
	}

	return 0;
}

static int chassis_info_init_by_json(struct chassis_info *chassis,
				     cJSON *json) {
	memset(chassis, 0, sizeof(*chassis));
	cJSON *type = cJSON_GetObjectItem(json, "type");
	if (cJSON_IsNumber(type))
		chassis->type = type->valueint;
	else
		ERROR_FIELD("chassis", "type");

	chassis->part_number =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "part_number"));
	chassis->serial_number =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "serial_number"));

	custom_field_init_by_json(chassis->custom_field, json);
	return 0;
}

static int board_info_init_by_json(struct board_info *board, cJSON *json) {
	memset(board, 0, sizeof(*board));
	cJSON *language_code = cJSON_GetObjectItem(json, "language_code");
	if (cJSON_IsNumber(language_code))
		board->language_code = language_code->valueint;
	else
		ERROR_FIELD("board", "language_code");

	board->mfg_time =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "mfg_time"));
	board->manufacturer =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "manufacturer"));
	board->product_name =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "product_name"));
	board->serial_number =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "serial_number"));
	board->part_number =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "part_number"));
	board->fru_file_id =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "fru_file_id"));

	custom_field_init_by_json(board->custom_field, json);

	return 0;
}

static int product_info_init_by_json(struct product_info *product,
				     cJSON *json) {
	memset(product, 0, sizeof(*product));
	cJSON *language_code = cJSON_GetObjectItem(json, "language_code");
	if (cJSON_IsNumber(language_code))
		product->language_code = language_code->valueint;
	else
		ERROR_FIELD("product", "language_code");

	product->manufacturer =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "manufacturer"));
	product->product_name =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "product_name"));
	product->part_number =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "part_number"));
	product->version =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "version"));
	product->serial_number =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "serial_number"));
	product->asset_tag =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "asset_tag"));
	product->fru_file_id =
	    cJSON_GetStringValue(cJSON_GetObjectItem(json, "fru_file_id"));

	return 0;
}

static int bin_generator(const char *filename, cJSON *json) {
	struct chassis_info chassis_info;
	struct chassis_info *p_chassis_info;
	struct board_info board_info;
	struct board_info *p_board_info;
	struct product_info product_info;
	struct product_info *p_product_info;
	cJSON *chassis = cJSON_GetObjectItem(json, "chassis");
	if (chassis == NULL || cJSON_IsNull(chassis))
		p_chassis_info = NULL;
	else {
		p_chassis_info = &chassis_info;
		chassis_info_init_by_json(p_chassis_info, chassis);
	}
	cJSON *board = cJSON_GetObjectItem(json, "board");
	if (board == NULL || cJSON_IsNull(board))
		p_board_info = NULL;
	else {
		p_board_info = &board_info;
		board_info_init_by_json(p_board_info, board);
	}
	cJSON *product = cJSON_GetObjectItem(json, "product");
	if (product == NULL || cJSON_IsNull(product))
		p_product_info = NULL;
	else {
		p_product_info = &product_info;
		product_info_init_by_json(p_product_info, product);
	}

	fru_bin_generator_by_info(filename, p_chassis_info, p_board_info,
				  p_product_info);
	return 0;
}

void usage(const char *name) {
	fprintf(stdout, "Usge: %s -j [fru.json] -b [fru.bin]\n", name);
	exit(-1);
}

int main(int argc, char **argv) {
	int opt = 0;
	const char *json_filename = NULL;
	const char *bin_filename = NULL;

	while ((opt = getopt(argc, argv, "j:b:h")) != -1) {
		switch (opt) {
			case 'j':
				json_filename = optarg;
				break;
			case 'b':
				bin_filename = optarg;
				break;
			case 'h':
				usage(argv[0]);
				break;
		}
	}

	if (json_filename == NULL || bin_filename == NULL) usage(argv[0]);

	FILE *fp = fopen(json_filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "open file %s:%s\n", json_filename,
			strerror(errno));
		exit(-1);
	}
	fseek(fp, 0, SEEK_END);
	long json_file_length = ftell(fp);
	rewind(fp);
	char buffer[json_file_length + 1];
	buffer[json_file_length] = 0;

	int r = fread(buffer, json_file_length, 1, fp);

	if (r != 1) {
		if (ferror(fp))
			fprintf(stderr, "read file %s:%s\n", json_filename,
				strerror(errno));

		fprintf(stderr, "read file error :%s\n", json_filename);
		fclose(fp);
		exit(-1);
	}
	fclose(fp);

	cJSON *json = cJSON_Parse(buffer);
	if (json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
			fprintf(stderr, "json parse error before %s\n",
				error_ptr);
		exit(-1);
	}

	bin_generator(bin_filename, json);
	cJSON_Delete(json);
}
