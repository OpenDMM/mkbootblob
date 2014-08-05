#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FILENAME 255

struct list_entry {
	char filename[MAX_FILENAME];
	uint32_t image_len;
	uint32_t dest_addr;
	uint32_t lba_pos;
	uint32_t lba_len;
	int type;
	int arc_index;
	struct list_entry *next;
};

static struct list_entry *entire_list;

static int get_filelength(char *filename, uint32_t *len)
{
	struct stat st;

	if(stat(filename, &st) != 0)
		return -1;

	*len = st.st_size;
	return 0;
}

static int validate_list(void)
{
	struct list_entry *element;
	char *type_lut[] = {"none", "kernel ", " logo  ", "binload", "", "", "", "", "  arc  "};
	static uint32_t lba_pos = 1;	//first block is for content-list

	if (entire_list == NULL) {
		printf("no elements given!\n");
		return -1;
	}

	printf("\n");
	printf("destination |    size    |  lba-pos | lba-len |  type   | filename\n");
	printf("-------------------------------------------------------------------\n");

	for (element = entire_list; element != NULL; element = element->next) {
		uint32_t filelen;
	
		if(get_filelength(element->filename, &filelen)) {
			printf("cannot get size of %s\n", element->filename);
			return -1;
		}

		/* we need 4K blocks! */
		filelen = (filelen + 4095) & ~4095;

		element->image_len = filelen;
		element->lba_pos = lba_pos;
		element->lba_len = (filelen + 512) / 512;

		lba_pos += element->lba_len;

		printf(" 0x%08x | %10d | %8d | %7d | %s | %s\n", element->dest_addr, element->image_len, element->lba_pos, element->lba_len, type_lut[element->type], element->filename);
	}
	printf("\n");

	return 0;
}


int main(int argc, char **argv)
{
	struct list_entry *element = NULL, *last_element;
	int c;
	int ofd;

	const char *output_filename = "out.bin";
	unsigned char block[512];
	uint32_t *block32 = (uint32_t *)block;
	uint32_t wp;
	

	while((c = getopt(argc, argv, "f:d:t:i:h")) != -1) {
		switch(c) {
			case 'f':
				last_element = element;
				element = malloc(sizeof(struct list_entry));
				element->next = 0;
				if(!entire_list)
					entire_list = element;
				else
					last_element->next = element;
				strncpy(element->filename, optarg, MAX_FILENAME);
				break;
			case 'd':
				element->dest_addr = strtol(optarg, NULL, 16);
				break;
			case 't':
				if(!strcmp(optarg, "kernel"))
					element->type = 1;
				else if(!strcmp(optarg, "bootlogo"))
					element->type = 2;
				else if(!strcmp(optarg, "binload"))
					element->type = 3;
				else if(!strcmp(optarg, "arc"))
					element->type = 8;
				break;
			case 'i':
				element->arc_index = strtol(optarg, NULL, 16);
				break;
			case 'h':
			default:
				printf("usage: %s [-f file.bin -d dest_addr -t kernel/bootlogo/binload [-i arcindex]] [-h]\n", argv[0]);
				return 1;
		}
	}

	if(validate_list()) {
		printf("vaildation of elements failed\n");
		return 1;
	}


	/* now compose the data */

	/* create the content list */
	memset(block, 0, sizeof(block));

	wp = 0;
	for (element = entire_list; element != NULL; element = element->next) {
		assert(wp * 4 < 512);
		block32[wp++] = element->image_len;
		block32[wp++] = element->lba_pos;
		block32[wp++] = element->dest_addr;
		if (element->type == 8)
			block32[wp++] = element->type + element->arc_index;
		else
			block32[wp++] = element->type;
	}

	ofd = open(output_filename, O_CREAT | O_WRONLY, S_IRWXU);
	if (ofd < 0) {
		fprintf(stderr, "Could not open %s for writing. %s\n",
			output_filename, strerror(errno));
		return 1;
	}

	if (write(ofd, block, 512) != 512) {
		fprintf(stderr, "write() failed.\n");
		return 1;
	}

	for (element = entire_list; element != NULL; element = element->next) {
		uint32_t lba;
		int ifd;

		ifd = open(element->filename, O_RDONLY);
		if(ifd <= 0) {
			printf("cannot open %s\n", element->filename);
			return 1;
		}

		for(lba = 0; lba < element->lba_len; lba++) {
			int rlen = read(ifd, block, 512);
			if(rlen < 512) {
				if (rlen < 0)
					rlen = 0;
				memset(&block[rlen], 0, 512-rlen);
			}
			if (write(ofd, block, 512) != 512) {
				fprintf(stderr, "write() failed.\n");
				return 1;
			}
		}

		close(ifd);
	}

	close(ofd);

	return 0;
}
