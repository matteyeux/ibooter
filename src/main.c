#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libusb-1.0/libusb.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

#include <include/ibooter.h>
#include <include/irecovery.h>
#include <include/img3.h>

#define MAXSTRING 128
#define ISSTRSIZE 3

static struct option longopts[] = {
	{ "img3",		required_argument,	NULL, 'i'},
	{ "load",		required_argument,	NULL, 'l'},
	{ "kickstart", 	required_argument,	NULL, 'k'},
	{ "mode",		no_argument,		NULL, 'm'},
	{ "shell",		no_argument,		NULL, 's'},
	{ "exit",		no_argument,		NULL, 'e'},
	{ "diags",		no_argument,		NULL, 'd'},
	{ "help",		no_argument, 		NULL, 'h'},
	{ NULL,			0,					NULL,  0 }
};

char *find_img_type(const char *binfile){
	FILE *fp;
	char *buf;
	int ch;
	size_t i = 0, j = MAXSTRING;
	char *img_type;

	img_type = malloc(sizeof(char) * 4);

	fp = fopen(binfile, "rb");
	buf = malloc(MAXSTRING);

	if (!fp){
		perror("fopen");
		return NULL;
	}

	if (!buf){
		return NULL;
	}

	while(!feof(fp)) {
		ch = fgetc(fp);
		if (isprint(ch)) {
			if(i > j) {
				buf = realloc(buf, j*2);
				j *= 2;
			}
			buf[i++] = (char) ch;
		}
		else {
			if( i > ISSTRSIZE ) {

				//printf("%6lu: ", ftell(fp) - i - 1);

				buf[i] = '\0';
				sprintf(img_type, "%c%c%c%c",buf[0], buf[1], buf[2], buf[3]);
				free(buf);
				fclose(fp);
				return (char *)img_type;
			}
			i = 0;
		}
	}
	free(buf);
	fclose(fp);
	return NULL;
}

void usage(char *argv[])
{
	char *name = NULL;
    name = strrchr(argv[0], '/');
	fprintf(stdout, "Usage : %s [OPTIONS]\n",(name ? name + 1: argv[0]));
	fprintf(stdout, " -i, --img3 [file] <tag>\tcreate IMG3\n");
	fprintf(stdout, " -l, --load [IMG3]\t\tload IMG3 file\n");
	fprintf(stdout, " -k, --kickstart [file] <tag>\tcreate and load img3\n");
	fprintf(stdout, " -m, --mode\t\t\tdevice mode\n");
	fprintf(stdout, " -s, --shell\t\t\tstart recovery shell\n");
	fprintf(stdout, " -d, --diags\t\t\tstart device in diagnostic mode\n");
	fprintf(stdout, " -h, --help\t\t\tprint help\n");
}

int main(int argc, char *argv[])
{
	int opt = 0;
	int optindex=0;
	int img3 = 0, load = 0;
	int kickstart = 0;
	int shell = 0;
	int exit_recovery = 0;
	int diag_mode = 0;

	char *file, *img3file, *tag;
	char* logfile = "recovery.log";

	if (argc < 2)
	{	
		usage(argv);
		return 0;
	}

	while((opt = getopt_long(argc, argv, "ilkmsedh", longopts, &optindex)) > 0)
	{
		switch(opt)
		{
			case 'i':
				img3 = 1;
				if (argc == 4){
					file = argv[2];
					tag = argv[3];
				} else if (argc == 3){
					file = argv[2];
					tag = find_img_type(file);
				}
				break;
			case 'l':
				load = 1;
				if (argc == 3){
					img3file = argv[2];
				}
				printf("%s\n", img3file);
				break;
			case 'k':
				printf("kickstart\n");
				break;
			case 'm':
				break;
			case 's' :
				shell = 1;
				break;
			case 'e' :
				exit_recovery = 1;
				break;
			case 'd':
				diag_mode = 1;
				break;
			case 'h' :
				usage(argv);
				break;
			default : 
				usage(argv);
 		}
	}

	if (img3) {
		char img3_out[128];
		if(file != NULL){
			create_image_preprocess(file, tag, "ibec.cool");
			create_image(tag);
			sprintf(img3_out, "%s.img3", file);
			output_image(img3_out);
		}
	}

	if (load)
	{	
		if(!is_IMG3(img3file) || img3file == NULL){
			printf("[e] %s is not an IMG3 file\n", img3file);
			return -1;
		}
		printf("%s\n", img3file);
		init_recovery();
		device_send(img3file, 1);
		close_recovery();
	}

	if (kickstart)
	{	char img3_out[128];
		if (img3) {
			if(file != NULL){
				create_image_preprocess(file, tag, "ibec.cool");
				create_image(tag);
				sprintf(img3_out, "%s.img3", file);
				output_image(img3_out);
			}
		}

		if(!is_IMG3(img3file) || img3file == NULL){
			printf("[e] %s is not an IMG3 file\n", img3file);
			return -1;
		}
		printf("%s\n", img3file);
		init_recovery();
		device_send(img3_out, 1);
		close_recovery();

	}
	if (shell)
	{	
		init_recovery();
		prog_console(logfile);
		close_recovery();
	}

	if (exit_recovery)
	{
		init_recovery();
		device_autoboot();	
		close_recovery();
	}

	if (diag_mode)
	{
		char *command = "diags";
		init_recovery();
		device_sendcmd(&command);
		close_recovery();
	}

	return 0;
}
