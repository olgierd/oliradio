#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <netdb.h>
#include <fftw3.h>
#include <math.h>
#include <X11/Xlib.h>

#define FFT_SIZE 700


#define czas_start()    clock_gettime(CLOCK_REALTIME, &t1) // pomiar czasu
#define czas_stop()     clock_gettime(CLOCK_REALTIME, &t2)
#define czas_oblicz()   czas = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec)
#define czas_print()    printf("%.6f;\n", czas)

timespec t1, t2;
double czas;

void error(const char *msg) {
	perror(msg);
	exit(0);
}



void makeCmd(unsigned char *command, unsigned int type, unsigned int data) {

	bzero(command, 5);

	command[0] = (char) type;
	command[1] = (data >> 24) & 0xff;
	command[2] = (data >> 16) & 0xff;
	command[3] = (data >> 8) & 0xff;
	command[4] = (data >> 0) & 0xff;

}

XImage *CreateTrueColorImage(Display *display, Visual *visual, unsigned char *image, int width, int height, unsigned char *data, unsigned char *image32, int line) {


	unsigned char *p = image32 + width*line*4;

	for (int j = 0; j < width; j++) {
		*p++ = (unsigned char) 255-data[j % FFT_SIZE];
		*p++ = (unsigned char) 255-data[j % FFT_SIZE];
		*p++ = (unsigned char) 255-data[j % FFT_SIZE];

		p++;
	}

//    for(i=0; i<width; i++)
//    {
//        for(j=0; j<height; j++)
//        {
//        	if(i & j) {
//        		*p++=0xff;
//        		*p++=0x0;
//        		*p++=0x0;
//        	}
//        	else {
//        		*p++=0xff;
//        		*p++=0xff;
//        		*p++=0xff;
//        	}
//
//
//        *p++;
//		}
//    }

	return XCreateImage(display, visual, 24, ZPixmap, 0, (char *)image32, width, height, 32, 0);

}

int getLineIndex(int width, int height, int line) {

	return line * width * 4;
}

void copyLines(void *dest, void *src, int width, int lines) {


	for(int i=0; i<lines; i++) {

		memcpy(dest+width*4*i, src+width*4*i, width*4);

	}

}


void redraw(Display *display, Window window, XImage *ximage, int width, int height, Visual *visual, unsigned char *tab, unsigned char *image32, int line) {

	ximage = CreateTrueColorImage(display, visual, 0, width, height, tab, image32, line);

	XPutImage(display, window, DefaultGC(display, 0), ximage, 0, 0, 0, 0, width, height);

	copyLines(image32, image32+(4*width), width, height-1);
//	memcpy(image32, image32 + getLineIndex(width, height, 300), width*(height-300)*4);
}

int main(int argc, char *argv[]) {

	if (argc < 6) {
		fprintf(stderr, "usage %s hostname port contrast frequency which\n", argv[0]);
		exit(0);
	}

	double contrast = atoi(argv[3]) / 10.0;
	int which = atoi(argv[5]);


	double out[FFT_SIZE];					// real fft output
	double hannwindow[FFT_SIZE];			// array with window function
	unsigned char buffer[FFT_SIZE*2];		// data read nby socket
	double a, b;

	fftw_complex *in_c;						// complex fft input
	fftw_complex *out_c;						// complex fft output
	fftw_plan p;
	unsigned char *outputTab = new unsigned char[FFT_SIZE];	// int fft output

	int width = FFT_SIZE, height = 400;
	unsigned char *image32 = (unsigned char *) malloc(width * height * 4);

	int line = 0;


	///////////////////////////////////////////////////////////// SETUP SOCKET

	int sockfd, portno, n;
	struct sockaddr_in serv_addr, clnt_addr;
	struct hostent *server;
	portno = atoi(argv[2]);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");
	server = gethostbyname(argv[1]);
	if (server == NULL) {
		fprintf(stderr, "ERROR, no such host\n");
		exit(0);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(portno);

	clnt_addr.sin_family = AF_INET;
	clnt_addr.sin_addr.s_addr = INADDR_ANY;
	clnt_addr.sin_port = htons(33333);

	if (bind(sockfd, (struct sockaddr *) &clnt_addr, sizeof(clnt_addr)) < 0)
		error("ERROR on binding");

	if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR connecting");

	///////////////////////////////////////////////////////////// SETUP TUNER

	unsigned char command[5];
	int frequency = atoi(argv[4]);

	makeCmd(command, 5, 54);
	write(sockfd, command, 5);

	makeCmd(command, 1, frequency);
	write(sockfd, command, 5);

	//////////////////////////////////////////////////////////////// INIT FFT

	in_c = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	out_c = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);

	p = fftw_plan_dft_1d(FFT_SIZE, in_c, out_c, 1, FFTW_ESTIMATE);

	////////////////////////////////////////////////////////////////  INIT WINDOW

	XEvent event;
	XImage *ximage = NULL;
	Display *display = XOpenDisplay(NULL);
	Visual *visual = DefaultVisual(display, 0);
	Window window = XCreateSimpleWindow(display, RootWindow(display, 0), 0, 0,
			width, height, 1, 0, 0);

	XSelectInput(display, window, ButtonPressMask | ExposureMask);

	XMapWindow(display, window);



	////////////////////////////////////////////////////////////////  INIT FFT WINDOW

	for (int i = 0; i < FFT_SIZE/2; i++) {
		hannwindow[i] = 0.5 * (1 - cos((2 * M_PI * i) / (FFT_SIZE/2 - 1)));
	}

//	czas_start();

	read(sockfd, buffer, 21);

	for (;;) {

		for (int z = 0; z < which; z++)
			read(sockfd, buffer, FFT_SIZE*2);

		for (int i = 0; i < FFT_SIZE; i++) {
			in_c[i][0] = buffer[2*i] * hannwindow[i/2] /255.0;
			in_c[i][1] = buffer[2*i+1] * hannwindow[i/2] /255.0;
		}


		for(int i=0;i<which; i++)
			fftw_execute(p);

		for (int i = 0; i < FFT_SIZE; i++) {
			a = out_c[i][0];
			b = out_c[i][1];
			out[i] = sqrt(a * a + b * b);
			out[i] *= contrast;
		}


//		czas_start();

		for (int i = 0; i <FFT_SIZE; i++) {
			outputTab[i] = ((((int) out[i] << 16) & 0xff) | (((int) out[i] << 8) & 0xff) | (((int) out[i]) & 0xff));
		}

		redraw(display, window, ximage, width, height, visual, outputTab, image32, height-1);

//		czas_stop();
//		czas_oblicz();
//		czas_print();

//		line++;
//
//		if(line==height) {
//			line = 0;
//			move = 1;
//		}


		if (XPending(display) > 0) {

			XNextEvent(display, &event);

			if(event.type == ButtonPress) {
//				printf("%d %d\n", event.xmotion.x, event.xmotion.y);
				frequency = frequency + ((event.xmotion.x - FFT_SIZE/2)*1000);
				makeCmd(command, 1, frequency);
				write(sockfd, command, 5);

				printf("Tuning to %d\n", frequency);
			}


		}


	}

	if (n < 0)
		error("ERROR reading from socket");

	close(sockfd);
	fftw_free(out);

	return 0;
}
