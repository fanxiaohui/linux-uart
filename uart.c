#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <syslog.h>
#include <sys/wait.h>

/*打开串口函数*/
int open_port(int fd,int comport)
{
	char *dev[]={"/dev/ttyS0","/dev/ttyS1","/dev/ttyS2"};
	long vdisable;
	if (comport==1)//串口 1
	{
		fd = open( "/dev/ttyS0", O_RDWR|O_NOCTTY|O_NDELAY);
		if (-1 == fd){
			perror("Can't Open Serial Port");
			return(-1);
		}

	}	
	else if(comport==2)//串口 2
	{
		fd = open( "/dev/ttyS1", O_RDWR|O_NOCTTY);
		if (-1 == fd){
			perror("Can't Open Serial Port");
			return(-1);
		}
	}

	else if (comport==3)//串口 3
	{
		fd = open( "/dev/ttyS2", O_RDWR|O_NOCTTY|O_NDELAY);
		if (-1 == fd){
			perror("Can't Open Serial Port");
			return(-1);
		}
	}
	/*恢复串口为阻塞状态*/

	if(fcntl(fd, F_SETFL, 0)<0)
		printf("fcntl failed!\r\n");

	else
		printf("fcntl=%d\r\n",fcntl(fd, F_SETFL,0));

	/*测试是否为终端设备*/

	if(isatty(STDIN_FILENO)==0)

		printf("standard input is not a terminal device\r\n");

	else
		printf("isatty success!\r\n");

	printf("fd-open=%d\r\n",fd);
	return fd;
}

int set_opt(int fd,int nSpeed, int nBits, char nEvent, int nStop)
{

	struct termios newtio,oldtio;

	/*保存测试现有串口参数设置,在这里如果串口号等出错,会有相关的出错信息*/
	if ( tcgetattr( fd,&oldtio) != 0) {
		perror("SetupSerial 1");
		return -1;
	}
	bzero( &newtio, sizeof( newtio ) );
	
	/*步骤一,设置字符大小*/
	newtio.c_cflag |= CLOCAL | CREAD;
	newtio.c_cflag &= ~CSIZE;
	
	/*设置停止位*/
	switch( nBits )
	{
	case 7:
		newtio.c_cflag |= CS7;
		break;
	case 8:
		newtio.c_cflag |= CS8;
		break;
	}
	/*设置奇偶校验位*/

	switch( nEvent )
	{
	case 'O': //奇数
		newtio.c_cflag |= PARENB;
		newtio.c_cflag |= PARODD;
		newtio.c_iflag |= (INPCK | ISTRIP);
		break;
	case 'E': //偶数
		newtio.c_iflag |= (INPCK | ISTRIP);
		newtio.c_cflag |= PARENB;
		newtio.c_cflag &= ~PARODD;
		break;
	case 'N': //无奇偶校验位
		newtio.c_cflag &= ~PARENB;
		break;
	}

	/*设置波特率*/

	switch( nSpeed )
	{
	case 2400:
		cfsetispeed(&newtio, B2400);
		cfsetospeed(&newtio, B2400);
		break;
	case 4800:
		cfsetispeed(&newtio, B4800);
		cfsetospeed(&newtio, B4800);
		break;
	case 9600:
		cfsetispeed(&newtio, B9600);
		cfsetospeed(&newtio, B9600);
		break;
	case 115200:
		cfsetispeed(&newtio, B115200);
		cfsetospeed(&newtio, B115200);
		break;
	default:
		cfsetispeed(&newtio, B115200);
		cfsetospeed(&newtio, B115200);
		break;
	}

	/*设置停止位*/
	if( nStop == 1 )
		newtio.c_cflag &= ~CSTOPB;

	else if ( nStop == 2 )
		newtio.c_cflag |= CSTOPB;

	/*设置等待时间和最小接收字符*/
	newtio.c_cc[VTIME] = 2;
	newtio.c_cc[VMIN] = 1;

	/*处理未接收字符*/
	tcflush(fd,TCIFLUSH);

	/*激活新配置*/
	if((tcsetattr(fd,TCSANOW,&newtio))!=0)
	{
		perror("com set error");
		return -1;
	}
	//printf("set done!\r\n");
	return 0;
}



/****************************************************************/
static struct flock tty_lock;

int char2temp(char* p_temp)
{
	int temp;
	//printf("len = %d \r\n", strlen(p_temp));
	if(strlen(p_temp) == 6)
		temp = (p_temp[0]-'0') * 10 + (p_temp[1] - '0');
	else if(strlen(p_temp) == 7)
		temp = (p_temp[0] - '0') * 100 + (p_temp[1] - '0') * 10 + (p_temp[2] - '0');
	else
		return -1;
	//printf("temp = %d \r\n", temp);
	return temp;
}

int tempf_open(void)
{
	int fd = open("/sys/class/hwmon/hwmon0/temp1_input", O_RDONLY);
	if(fd < 0)
	{
		perror("temp file open fail: "); 
		return -1;
	}
	return fd;
}

int cputemp_get(int fd)
{
	char temp_char[10] = {};

	int res = read(fd, temp_char, 10);
	if(res < 0)
	{
		perror("temp file read fail: "); 
		return -1;
	}

	res = lseek(fd, SEEK_SET, 0);

	return char2temp(temp_char);
}

static int fd_tty;
static int signalno = 0;
void* tty_rcv(void* fd_tty)
{
	unsigned char dataframe[4] = {0, 0xef, 0xff, 0xee};
	int fd = *((int*)fd_tty);
	char tty_signal[64] = {};
	//unsigned char power_signal = 0xff;
	int res = 0;
	while(1){
		res = read(fd, tty_signal, 64);
		
		if(!strncmp(tty_signal, "poweroff", 8)){
			if(fcntl(fd, F_SETLK, &tty_lock) == 0){
				write(fd, dataframe, 4);
				close(fd);
			}
			system("sync");
			system("poweroff -f");
			break;
		} else {
			memset(tty_signal, '\0', 64);
		}
	}
	return NULL;
}

void sighandler(int arg)
{
	// printf("get a signal = %d \n ", arg);
	char data;
	signalno = arg;
	return ;
}

void sigchld_handler(int arg)
{
	int status = 0;
	while(waitpid(-1, &status, WNOHANG) > 0);
	return;
}

char hddtemp_get(void)
{
	unsigned char temp = 0;
	char temp_str[12] = {};
	int status = 0;
	FILE* pf = NULL;
	
	//程序运行<有时>会产生僵尸进程, 怀疑是popen子进程产生, 故作此处理, 
	//但popen产生僵尸进程的问题未证实, 如此处理效果也未证实
	signal(SIGCHLD, sigchld_handler);   
	//popen调用产生的子进程退出后会发SIGHLD信号
	pf = popen("/bin/sh /tmp/run/hddtemp", "r");
	if(NULL == fgets(temp_str, 12, pf)) {
		return 0;
	}

	temp = atoi(temp_str);

	printf("hddtemp = %d \n", temp);
	pclose(pf);
	return temp;
}

void hddtemp_get_init(void){
//  获取硬盘温度脚本代码
//  获取硬盘温度信息的目的： 
//  1. 控制关机保护硬盘，未超过最高温度忽略
//  2. 控制风扇用于散热， 未超过warnlevel忽略
char get_hddtempsh[] = "diskname=`cat /proc/partitions | grep \"sd[a-z]$\" | awk '{print $4}'`\n \
for i in $diskname; do\n \
	scsidevinfo=`find /sys/class/scsi_device/*/device/ -name $i`\n \
	ishdisk=`cat $scsidevinfo/removable`\n \
	if [ \"$ishdisk\" = 0 ]; then\n \
		disksymbol=`echo $scsidevinfo | awk -F '/' '{print $5}' | awk -F ':' '{print $1}'`\n \
		ls /proc/scsi/usb-storage/ 2>/dev/null | grep ^$disksymbol$\n \
		if [ $? != 0 ]; then\n \
			tempinfo=`lancehddtemp /dev/$i 2>/dev/null`\n \
			model=`echo $tempinfo | awk -F':' '{print $2}'` \n \
			model=`echo ${model% *}`	\n \
			temp=`echo $tempinfo | awk -F':' '{print $3}' | awk '{print $1}'`\n \
			expr \"$temp\" + \"0\" 1>/dev/null 2>/dev/null \n \
			if [ $? != 0 ]; then \n \
				temp=\"0\" \n \
			fi \n \
			hdparm -H /dev/$i 2>/dev/null | grep \"drive temperature in range:\" | grep -v \"yes\"\n \
			# 确定超过最高温度 直接关机并告知mcu\n \
			if [ $? = 0 ]; then \n \
				echo 255 \n \
				exit \n \
			#用于处理温度不高于上限或无法判断的情形\n \
			else \n \
				warnlevel=`grep \"$model\" /etc/config/hd.list | awk -F':level:' '{print $2}'`\n \
				if [ -z \"$warnlevel\" ]; then \n \
					warnlevel=48 \n \
				fi \n \
				upperlevel=`grep \"$model\" /etc/config/hd.list | awk -F':level:' '{print $3}'`\n \
				if [ -z \"$upperlevel\" ]; then \n \
					upperlevel=55 \n \
				fi \n \
				#用于处理温度不高于上限或无法判断的情形\n \
				if [ \"$temp\" -gt \"$warnlevel\" ] && [ \"$temp\" -le \"$upperlevel\" ];then \n \
					echo 110 ##不是具体正确值，只表示较高温度值， 方便后面处理 \n \
					exit \n \
				#超过最高温度，关机并告知mcu\n \
				elif [ \"$temp\" -gt \"$upperlevel\" ]; then \n \
					echo 255 \n \
					exit \n \
				else\n\
					echo 35	##不是具体的正确值， 只表示较低温度值， 方便后面处理\n \
				fi \n \
			fi \n \
		fi\n \
	fi\n \
done\n \
";

	int fd = open("/tmp/run/hddtemp", O_RDWR | O_CREAT, 0755);
	if(fd < 0)
		perror("open"), exit(-1);
	int res = write(fd, get_hddtempsh, strlen(get_hddtempsh));
	if(res < 0)
		perror("write"), exit(-1);
	close(fd);
}

#define LOCKFILE "/var/run/uart.pid"
#define LOCKMODE S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH

int lockfile(int fd)
{
	struct flock flk;
	flk.l_type = F_WRLCK;
	flk.l_start = 0;
	flk.l_whence = SEEK_SET;
	flk.l_len = 0;
	return fcntl(fd, F_SETLK, &flk);
}

int already_running(void)  // 只允许程序运行一次
{
	int fd;
	char buf[16] = {};
	if((fd = open(LOCKFILE, O_RDWR | O_CREAT, LOCKMODE)) < 0){
		syslog(LOG_ERR, "can't open %s: %s", LOCKFILE, strerror(errno));
		exit(1);
	}
	if(lockfile(fd) < 0){
		if(errno == EACCES || errno == EAGAIN){
			close(fd);
			return 1;
		}
		syslog(LOG_ERR, "can't lock %s: %s", LOCKFILE, strerror(errno));
		exit(1);
	}
	ftruncate(fd, 0);
	snprintf(buf, 16, "%ld", (long)getpid());
	write(fd, buf, strlen(buf)+1);
	return 0;
}

int main(int argc, char* argv[])
{
	int nwrite;
	unsigned char dataframe[4] = {0, 0xef, 0x32, 0xee};
	//unsigned char temp = 50;  //初始化到正常的温度值, 以防第一次发送出现异常
	int port = 2;

	if(already_running()){
		syslog(LOG_ERR, "uart progress is already running");
		exit(1);
	}
	signal(SIGUSR1, sighandler);   // 信号SIGUSR1用于恢复到正常读取硬盘和cpu的温度的流程中去
	signal(SIGUSR2, sighandler);  // 以下3个信号用于设置关机, 初始化, 恢复出厂设置的3种状态码值, 最后统一由串口发给mcu
	signal(3, sighandler);
	signal(4, sighandler);

	tty_lock.l_type = F_WRLCK;
	tty_lock.l_whence = SEEK_SET;
	tty_lock.l_start = 0;
	tty_lock.l_len = 10;
	
	if((fd_tty=open_port(fd_tty, port))<0){//打开串口
		perror("open_port error");
		return;
	}
	if((set_opt(fd_tty,115200,8,'N',1))<0){//设置串口
		perror("set_opt error");
		return;
	}
	
	int fd_tempf = tempf_open();
	pthread_t tid;
	pthread_create(&tid, NULL, tty_rcv, (void*)&fd_tty);
	
	pthread_detach(tid);
	
	hddtemp_get_init();  //产生硬盘温度获取的脚本
	char temp_hdd = 30;  //初始值赋为正常值
	unsigned char temp_cpu = 75; //初始值赋为正常值
	char flag_tempget = 90;
	for( ; ; ){
		if(signalno == SIGUSR2){
			dataframe[2] = 0xff; // 关机, 用于脚本编程中关机
			flag_tempget = 90;
		} else if(signalno == 3){
			dataframe[2] = 0xfe; // 初始化
			flag_tempget = 90;
		} else if(signalno == 4){
			dataframe[2] = 0xfd; // 恢复出厂设置
			flag_tempget = 90;
		} else { //无信号或接收信号1后, 正常读取硬盘, cpu的温度
			if(flag_tempget++ >= 90){  // 每90s获取一次温度
				flag_tempget = 0;
				temp_cpu = (unsigned char)cputemp_get(fd_tempf);
				temp_hdd = hddtemp_get();
				if(temp_hdd < 0)  // 防止环境温度极冷刚开机时硬盘温度小于0引发未知错误, 没硬盘时硬盘检测到的温度会是0
					temp_hdd = 0;
				
				// 硬盘温度没有获取正确值,只有70,110,0xff, 分别表示硬盘温度正常和较高需要风扇加速散热,超过最高温度要关机保护
				if(temp_hdd == 110)
					dataframe[2] = temp_hdd;
				else if(temp_hdd == 0xff){
					dataframe[2] = temp_hdd;
					if(fcntl(fd_tty, F_SETLK, &tty_lock) == 0){   //文件锁, 防止与关机检测线程中写tty时冲突
						nwrite=write(fd_tty, dataframe, 4);//写串口
						lseek(fd_tty, SEEK_SET, 0);
						tty_lock.l_type = F_UNLCK;
						fcntl(fd_tty, F_SETLK, &tty_lock);
					}
					sleep(1);
					system("poweroff -f");
				}
				else
					dataframe[2] = temp_cpu;

				// printf("temp = %d, temp_hdd = %d, temp_cpu = %d \n", temp, temp_hdd, temp_cpu);  //没硬盘时温度temp_hdd = 27
			}
		}
		if(fcntl(fd_tty, F_SETLK, &tty_lock) == 0){   //文件锁, 防止与关机检测线程中写tty时冲突
			nwrite=write(fd_tty, dataframe, 4);//写串口
			lseek(fd_tty, SEEK_SET, 0);
			tty_lock.l_type = F_UNLCK;
			fcntl(fd_tty, F_SETLK, &tty_lock);
		}
		sleep(1);
	}
	close(fd_tempf);
	close(fd_tty);
	return;
}

