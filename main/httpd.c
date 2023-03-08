#pragma warning(disable:4996)	// 禁用4996错误提示

#include <stdio.h>
#include <string.h>

// 访问文件属性要用到的头文件
#include <sys/types.h>
#include <sys/stat.h>

// 网络通信相关的头文件和库
#include <WinSock2.h>
#pragma comment(lib, "WS2_32.lib")

// 打印一行数据（可以定义一个函数实现，也可以用宏替换实现，由于代码比较简单，定义函数开销更大，这里就直接定义成宏）
// [方法名 - 行号]数组名=打印内容。#拼接内容。VS2015中纯C语言文件识别不了__func__，但可以识别__FUNCTION__
#define PRINTF(str) printf("[%s - %d]"#str"=%s\n", __FUNCTION__, __LINE__, str);
//void myPrintf(const char* str) { printf("[%s - %d]"#str"=%s\n", __func__, __LINE__, str); }

/* 专门的报错工具函数：用于打印错误提示，并结束程序。
参数：提示对象的名称。
*/
void error_die(const char* str) {
	// perror 函数会先把传来的字符串打印出来，再打印错误原因。
	perror(str);
	// 结束程序
	exit(1);
}

/* 实现网络的初始化（服务器端的6步准备工作）。
返回值：套接字。（准确讲是服务器端的套接字）
	在网络开发中，套接字，就相当于一个 “网络插座”，网络通信，就是通过这个“插座” 收发信息的，相当于一个电话机的电话线插槽。
	我们现在写的是一个网站服务器，用户上网通过浏览器，输入网址，输入回车，就会放松一个请求，来访问我们的网页，请求经过互联网送到后端（服务器），请求需要的资源（网页、视频etc.）。
	所以整个架构的两端是浏览器和服务器。浏览器是现成的，我们这里只需要实现后端服务器即可。
	后端之所以能够对前端提供服务，是因为有一个很重要的东西--套接字，不管是浏览器那，还是服务器都是通过套接字来实现网络间的通信。
参数：port 端口号。
	在创建服务器时，还必须要指定一个端口号。当一台服务器，同时对外提供多种服务时，比如 WEB 服务，远程登录服务等等，就需要使用 “端口号”，对不同的服务进行区别。每个服务，都有自己唯一的端口号。
	一台服务器往往会同时对外提供很多功能，通过端口号来指定要对客户端提供哪种服务，每个服务都有一个唯一的端口号。
	有些服务拥有自己的默认端口号。
	如果*port的值是0，就自动分配一个可用的端口。
	参数写成指针的形式是为了向开源项目 tinyhttpd 借鉴学习。
	这里unsigned short是网路开发规范类型，合法范围是 [0,65535]。
注：服务端要做的6项准备工作不需要刻意记，理解、多用就行了。
*/
int startup(unsigned short* port) {
	// 一、网络通信的的初始化（Linux中不需要这一步）
	// 返回值：0表示初始化成功。
	// 参数1：网络通信的协议。MAKEWORD(1,1)表示1.1版本，前面是主版本，后面是子版本。
	// 参数2：初始化数据要保存的位置。
	WSADATA initData;
	int ret = WSAStartup(MAKEWORD(1, 1), (LPWSADATA)&initData);
	// 初始化失败。
	if (ret != 0) {
		// 打印错误提示，并结束程序。
		error_die("WSAStartup");
	}

	// 二、创建套接字
	// 返回值：返回一个套接字，-1表示创建失败。
	// 参数1：套接字的类型。PF_INET表示IPV4，是AF_INET的宏替换。套接字一般分为两种：网络套接字（互联网通信）和文件套接字（用于进程间通信(IPC)，电脑内部不同程序之间的通信，可理解为Linux内核底层的特殊文件）。这里用的是网络套接字。
	// 参数2：套接字的数据包，通常分两种类型：数据流通信和数据报通信。这里用的是数据流，它是面向连接的，更安全。
	// 参数3：（数据流）通信的具体协议。可以直接填0，或者指定具体要使用的协议。这用的是TCP协议。
	SOCKET server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	// 创建套接字失败。（一般不会失败）
	if (server_sock == -1) {
		// 打印错误提示，并结束程序。
		error_die("socket");
	}

	// 三、设置套接字属性（设置端口可复用）（本步骤并非必要步骤）
	// 本步骤是为了解决，关闭服务器，再次打开服务器，程序无法正常跑起来的问题。因为有一个默认端口，刚死亡，会出现一个“假死”，端口还处于被占用状态，所以无法再使用这个端口了。
	// 我们想要实现的是，不管何时关闭服务器，只要打开服务器就能立即使用。本步骤就是为了实现这一目的而服务的。
	// 返回值：-1表示设置失败。
	// 参数1：要设置属性的套接字。
	// 参数2：套接字级别。
	// 参数3：要设置的属性。这是想要让端口可以重复使用。
	// 参数4：属性值。1表示设置的属性生效了。
	// 参数5：属性长度。
	int optVal = 1;									// 属性值
	// 设置套接字属性失败。
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optVal, sizeof(optVal)) == -1) {
		// 打印错误提示，并结束程序。
		error_die("setsockopt");
	}

	// 配置服务器端的网络地址
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));	// 初始化内存块中的数据为0
	server_addr.sin_family = AF_INET;				// 设置网络地址的类型为网络地址（在创建套接字时设置过套接字类型）。
	server_addr.sin_port = htons(*port);			// 设置端口。htons 函数将主机字节顺序转为网络字节顺序。
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);// 设置服务端ip地址，INADDR_ANY表示接受任意客户端的访问，其实就是0。

	// 四、绑定套接字和网络地址
	// 返回值：小于0表示绑定失败。
	// 参数1：要绑定的套接字。
	// 参数2：要绑定的网络地址。
	// 参数3：网络地址的长度。
	// 绑定失败。
	if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		// 打印错误提示，并结束程序。
		error_die("bind");
	}

	// 五、动态分配一个端口号
	int nameLen = sizeof(server_addr);
	if (*port == 0) {
		// 参数1：套接字。这应当传入服务器的套接字 server_sock。
		// 参数2：（服务器的）网络地址。
		// 参数3：地址长度。
		// 动态获取端口号失败。
		if (getsockname(server_sock, (struct sockaddr*)&server_addr, &nameLen) < 0) {
			// 打印错误提示，并结束程序。
			error_die("getsockname");
		}

		// 分配端口号
		*port = server_addr.sin_port;
	}

	// 六、创建监听队列
	// 返回值：小于0表示创建失败。
	// 参数1：为哪个套接字创建监听队列。
	// 参数2：监听队列长度。经测试，其取值一般很小，这里取5（你取个20也没什么问题）。
	// 创建失败。
	// 注：这里一而再，再而三地去判断返回值是因为网络是很不稳定的，很容易出问题，所以必须要做好检查工作，以保证我们服务端的可靠性。
	if (listen(server_sock, 5) < 0) {
		// 打印错误提示，并结束程序。
		error_die("listen");
	}

	return server_sock;
}

// 从指定的客户端套接字，读取一行数据，保存到 buff 中。
// 返回值：成功返回实际读取到的字节数，否则 to do.。
// 参数1：套接字。
// 参数2：保存的内存位置。
// 参数3：内存块的大小（可以保存的字符个数）。
int get_line(SOCKET sock, char* buff, unsigned int size) {
	// 遇到回车符就认为是读取到了一行数据
	char c = 0;			// 结束符'\0'
	unsigned int i = 0;	// 下标
	// 没有读到回车符，并且下标合法
	// 在这里，\r\n才是真正的“回车符”
	while ((c != '\n') && (i < size - 1)) {
		// 读取数据
		// 返回值：实际读取字节数，失败返回非正值。
		// 参数1：从哪个套接字中读取数据。
		// 参数2：读取的数据存到哪里。
		// 参数3：每次读取字节字节数。
		// 参数4：xxx标志，给0表示使用默认参数，阻塞式读取，直到读取到1个字符位置。
		int n = recv(sock, &c, 1, 0);
		// 读取成功
		if (n > 0) {
			// 读取到了'\r'，那么理论上下一个字符就应该是'\n'
			if (c == '\r') {
				// （瞄一眼）检查下一个字符是什么，是不是'\n'，不从缓存区中取出（这样比较安全）
				n = recv(sock, &c, 1, MSG_PEEK);
				// 如果下个字符确实是'\n'
				if (n > 0 && c == '\n') {
					recv(sock, &c, 1, 0);	// 读取这个'\n'，保存到c中
				}
				// 否则
				else {
					c = '\n';				// 强制赋值一个'\n'，保存到c中
				}
			}
			// 保存读取到的字符
			buff[i++] = c;
		}
		// 读取失败
		else {
			// 容错处理
			//break;
			c = '\n';	// 借鉴自 tinyhttp
		}
	}

	buff[i] = 0;		// 结束符'\0'
	return i;
}

// 向指定客户端（套接字）发送一个提示还没有实现的错误页面
void unimplement(SOCKET client) {
	// to do.
}

// 向指定客户端（套接字）发送一个网页不存在的提示信息，也就是404响应网页
void not_found(SOCKET client) {
	// 1. 把剩下的请求头部行的所有数据读取完毕
	char buff[2048] = { 0 };		// 缓冲区

	// 状态行：错误编号404，发送数据改为NOT FOUND
	strcpy(buff, "HTTP/1.0 404 NOT FOUND\r\n");
	// 把数据发送给客户端（浏览器）
	send(client, buff, strlen(buff), 0);

	// 把消息头部行（第2至N行）发送给客户端（浏览器）
	// 服务器相关信息直接拷贝过来，服务器名称、版本号随便搞
	strcpy(buff, "Server: YouShaoHttpd/0.1\r\n");
	send(client, buff, strlen(buff), 0);

	// 文件类型（这里发送的404响应网页固定为text/html类型）
	strcpy(buff, "Content-type: text/html\r\n");
	send(client, buff, strlen(buff), 0);

	// 空行
	strcpy(buff, "\r\n");
	send(client, buff, strlen(buff), 0);

	// 2. 发送404网页内容（由于网页内容比较简单，这里就直接把实现代码写到这里，不用读取html文件的方式了）
	// 直接把html代码当作字符串发送，转义字符、续行符\（后面什么字符也不要写，比如空格，符号）。
	sprintf(buff,
		"<!DOCTYPE HTML>																			\
		<HTML>																						\
			<HEAD>																					\
				<meta charset=\"utf-8\" />															\
				<TITLE>404 Page</TITLE>																\
				<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">			\
			</HEAD>																					\
			<BODY>																					\
				<p style=\"width: 100%;text-align: center;font-size: 140px;height: 40px;\">404</p>	\
				<p style=\"width: 100%;text-align: center;font-size: 40px;\">Page Not Found</p>		\
				<p style=\"width: 100%;text-align: center;font-size: 18px;\">Please reenter the URL or contact administrator.</p>\
				<p style=\"width: 100%;text-align: center;font-size: 18px;\">The resource is unavailable!</p>\
			</BODY>																					\
		</HTML>");
	send(client, buff, strlen(buff), 0);
}

// 向浏览器发送响应包的文件头部信息
void headers(SOCKET client, const char* type) {
	char buff[1024] = { 0 };		// 缓冲区

	// 第一行（状态行）直接拷贝过来
	// char *strcpy(char *dest, const char *src) 把 src 所指向的字符串复制到 dest。
	// 返回值：该函数返回一个指向最终的目标字符串 dest 的指针。
	// 参数1：dest -- 指向用于存储复制内容的目标数组。
	// 参数2：src  -- 要复制的字符串。
	strcpy(buff, "HTTP/1.0 200 OK\r\n");
	// 把第一行发送给客户端（浏览器）
	// 返回值：发送成功，返回实际发送的字节数，失败返回...。
	// 参数1：接收者。
	// 参数2：发送的数据。
	// 参数3：数据的长度。
	// 参数4：0表示默认参数。
	send(client, buff, strlen(buff), 0);

	// 把消息头部行（第2至N行）发送给客户端（浏览器）
	// 服务器相关信息直接拷贝过来，服务器名称、版本号随便搞
	strcpy(buff, "Server: YouShaoHttpd/0.1\r\n");
	send(client, buff, strlen(buff), 0);

	// 文件类型
	/*strcpy(buff, "Content-type: text/html\r\n");
	send(client, buff, strlen(buff), 0);*/
	char typeBuff[1024] = { 0 };
	sprintf(typeBuff, "Content-type: %s\r\n", type);
	send(client, typeBuff, strlen(typeBuff), 0);

	// ...

	// 空行
	strcpy(buff, "\r\n");
	send(client, buff, strlen(buff), 0);

	// 响应正文
	// to do.
}

// 发送请求的资源信息（真正文件内容的发送）
void cat(SOCKET client, FILE *resource) {
	// 思路：读、发、读、发...
	// tinyhttpd 一次读一个字节，然后就把这个字节发送给浏览器，显然这样很慢
	// 我们可以提高每次发送数据的大小，但是最多不能超过4096个字节
	char buff[4096] = { 0 };			// 缓冲区
	int ret = 0;						// 实际读取/发送的字节数
	int read_count = 0;					// 成功读取的字节数
	int send_count = 0;					// 成功发送的字节数
	
	// 读取文件资源（最多读取4096个字节）
	while (1) {
		// fread 从文件中读取数据
		// 返回值：成功，返回实际读取的字节数，否则返回一个负数。
		// 参数1：缓冲区。
		// 参数2：每次读多少个字节。
		// 参数3：读取次数。
		// 参数4：文件指针。
		ret = fread(buff, sizeof(char), sizeof(buff), resource);
		read_count += ret;
		// 读取失败
		if (ret <= 0) {
			break;			// 跳出循环
		}
		// 读取成功
		// 发送数据给客户端（浏览器）
		ret = send(client, buff, ret, 0);
		send_count += ret;
	}

	printf("向浏览器成功发送了[%d/%d]字节数据。\n", read_count, send_count);
}

// 根据文件名获取文件头类型（可根据需要增加识别的文件类型）
const char* getHeadType(const char* fileName) {
	const char* ret = "text/html";				// 默认文件类型
	// char *strrchr(const char *str, int c) 在参数 str 所指向的字符串中搜索最后一次出现字符 c（一个无符号字符）的位置。
	// 返回值：该函数返回 str 中最后一次出现字符 c 的位置。如果未找到该值，则函数返回一个空指针。
	// 参数1：C 字符串。
	// 参数2：要搜索的字符。以 int 形式传递，但是最终会转换回 char 形式。
	const char* p = strrchr(fileName, '.');
	if (!p) return ret;
	
	p++;
	// 如果文件名以.css结尾
	if (!strcmp(p, "css")) ret = "text/css";
	// 如果文件名以.jpg或.jpeg结尾
	else if (!strcmp(p, "jpg") || !strcmp(p, "jpeg")) ret = "image/jpeg";
	// 如果文件名以.png结尾
	else if (!strcmp(p, "png")) ret = "image/png";
	// 如果文件名以.js结尾
	else if (!strcmp(p, "js")) ret = "application/x-javascript";

	return ret;
}

// 向指定客户端（套接字）发送资源
void server_file(SOCKET client, const char *fileName) {
	unsigned int numchars = 1;			// 保存际读取的字节数（如果类型是char，有可能发生溢出，溢出值maybe负数）
	char buff[1024] = { 0 };			// 缓冲区。存储读取的一行数据，最大可存储1KB数据

	// 1. 读取请求包剩下的所有数据（请求头部行的数据）（尾行只有一个空行，此时strcmp返回0，跳出循环）
	while (numchars > 0 && strcmp(buff, "\n")) {
		numchars = get_line(client, buff, sizeof(buff));
		buff[numchars] = '\0';			// 结束符
		PRINTF(buff);					// 打印读取到的数据
	}

	// 2. 服务器要先从硬盘中把文件读取出来，然后再发送给浏览器
	// 2.1 读取文件
	FILE *resource = NULL;
	// 判断文件类型，选择相应的打开方式（可通过后缀名判断，只要是.html结尾的就用"r"的方式打开）
	/*if (strcmp(fileName, "htdocs/index.html") == 0) {// 这里就先简单判断
		resource = fopen(fileName, "r");	// 打开文本文件（可读，非二进制）
	}*/
	const char* p = strrchr(fileName, '.');
	if (!strcmp(++p, "html")) {
		resource = fopen(fileName, "r");	// 打开文本文件（可读，非二进制）
	}
	else {
		resource = fopen(fileName, "rb");	// 打开二进制文件（可读，二进制）
	}
	// 打开文件失败
	if (resource == NULL) {
		// 向客户端发送一个资源不存在的提示页面
		not_found(client);
	}
	// 打开文件成功
	else {
		// 2.2 发送资源给浏览器
		// HTTP协议规定，服务器和浏览器之间通信要遵循相关约束的行内标准：
		// 服务器在向浏览器发送资源之前要先发送一个特定的“文件头”，一个特定的头部信息，
		// 浏览器收到这个头部信息之后，才会接收后面的文件，
		// 头部信息格式遵循一定的规则，
		// 我们可以定义一个接口（可复用），用于服务器向浏览器发送头部信息，
		// 2.2.1 向浏览器发送一个文件头部信息
		headers(client, getHeadType(fileName));
		// 2.2.2 发送请求的资源信息
		// 请求资源信息可能是各种各样的，所以这里也把他封装成函数
		cat(client, resource);
		// 打印提示信息
		printf("资源发送完毕！\n");
	}

	// 关闭文件
	fclose(resource);
}

// 处理用户请求的线程函数
// 返回值：统一为 DWORD。
// 参数：客户端套接字。
// 因为它是Windows系统的线程函数，所以返回值和函数名之间要加一个关键字 WINPAI，两边空格隔开。
DWORD WINAPI accept_request(LPVOID arg) {
	// 浏览器通过HTTP协议收发数据。
	/* 先来了解一下接收浏览器的WEB请求的8个步骤（前四个是常规的网页访问的4个步骤）：
	1-用户输入网址，按下回车，浏览器向HTTP服务器发起网页访问请求：发送GET请求包；
	2-HTTP服务器向浏览器发响应包，发送被请求的网页；
	3-浏览器根据收到的网页内容，再次向HTTP服务器发送GET请求包，申请获取网页中包含的图片、JS、CSS等文件；
	4-HTTP服务器向浏览器发响应包，发送被请求的资源；
	5-用户在网页中填写数据，点击提交，向HTTP服务器发送POST请求；
	6-HTTP服务器把POST请求交给CGI处理（CGI可以理解是一个专门处理用户的动态请求的网站服务器）；
	7-CGI发送处理结果；
	8-HTTP服务器向浏览器发送POST请求的响应包。
	*/

	// 服务器解析GET请求包
	// 解析思路：读取一行数据，然后对其进行解析。

	// 1. 读取一行数据（封装成函数）
	// " GET / HTTP/1.1\n"
	char buff[1024] = { 0 };			// 缓冲区。存储读取的一行数据，最大可存储1KB数据，一行数据没那么大的。
	SOCKET client = (SOCKET)arg;		// 客户端套接字
	int numchars = get_line(client, buff, sizeof(buff));// 读取一行数据，返回实际读取的字节数。
	PRINTF(buff);						// 打印读取到的数据

	// 2.1 解析请求方法（注意方法前面和后面都有可能存在若干空白字符，如空格、制表符、回车符等）
	// 这里且认为行首不存在空白字符，只考虑行中是否存在空白字符
	// 使用 isspace() 方法检查一个字符是否是空白字符，是，就跳过
	// i < sizeof(method) - 1，减1是为结束符保留一个字节
	char method[255] = { 0 };			// 存储请求方法名
	int i = 0, j = 0;					// 索引变量，j用于从buff中取字符，i用于写字符
	// 2.1.1 保存方法名到method中
	while (!isspace(buff[j]) && i < sizeof(method) - 1) {
		method[i++] = buff[j++];
	}
	method[i] = 0;						// 结束符'\0'
	PRINTF(method);						// 打印读取到的方法名
	// 2.1.1 检查请求的方法，本服务器是否支持
	// 这里只考虑 GET 和 POST 两种请求方法，方法名不区分大小写
	// 如果不支持，就向浏览器返回一个错误提示页面（可以封装一个接口来实现该方法）
	// 使用 stricmp 方法 <string.h>，但VS报错4996，
	// 解决办法：禁用4996 或者 在项目属性中关闭SDL检查 或者 使用 _stricmp。
	if (stricmp(method, "GET") && stricmp(method, "POST")) {
		unimplement(client);
		return 0;						// 直接返回0，停止服务
	}

	// 2.2 解析资源文件的路径
	// www.youshao.com/abc/test.html
	// " GET /abc/test.html HTTP/1.1\n"
	char url[255] = { 0 };				// 存放请求的资源的路径
	i = 0;
	// 2.2.1 跳过资源路径前面的空白符
	while (isspace(buff[j]) && j < sizeof(buff)) j++;
	// 2.2.2 保存资源的路径到url中
	while (!isspace(buff[j]) && j < sizeof(buff) && (i < sizeof(url) - 1)) {
		url[i++] = buff[j++];
	}
	url[i] = 0;							// 结束符'\0'
	PRINTF(url);						// 打印请求资源的路径

	// 2.2.3 得到请求资源的完整路径
	// 网址：www.youshao.com/
	// IP地址：127.0.0.1/
	// 端口号：8000
	// 资源路径url：/（这并不是真正的资源路径）
	// 我们现在要做的就是把请求资源的完整路径搞出来
	// 按照后端开发通用的方式，我们会把所有相关的资源放到一个目录下，比如htdocs，我们需要在项目中创建这个目录
	// 把该目录拼接到之前获取到的资源路径url后面即可。
	char path[512] = "";				// 存储资源完整路径的字符串
	sprintf(path, "htdocs%s", url);		// 格式化字符串
	// 遵循网络服务器开发的一个普遍习惯，如果没有明确指明访问哪个资源文件时，默认访问资源路径下的index.html
	// 那么我们访问的资源的完整路径就是：/htdocs/index.html
	// 如果path中的最后一个字符是 '/'，就把"index.html"拼接到后面
	if (path[strlen(path) - 1] == '/') {
		strcat(path, "index.html");			// 拼接字符串
	}
	PRINTF(path);							// 打印请求资源的完整路径
	// 但是如果路径是127.0.0.1/test，这里test就是一个目录名
	// 使用 stat 方法访问文件属性，文件 or 目录，需要包含头文件 <sys/types.h> 和 <sys/stat.h>。
	// 返回值：成功则返回0，失败返回-1，错误代码存于errno。
	struct stat status;						// 用于保存文件属性
	// 访问失败（资源文件不存在，服务器访问不到//，如网页丢失或者网址输错）//127.0.0.1:8000/abc.html ---> htdocs/abc.html
	if (stat(path, &status) == -1) {
		printf("资源文件不存在，服务器访问不到！\n");
		// 读取请求包剩下的所有数据（请求头部行的数据）（尾行只有一个空行，此时strcmp返回0，跳出循环）
		while (numchars > 0 && strcmp(buff, "\n")) {
			numchars = get_line(client, buff, sizeof(buff));
			//PRINTF(buff);					// 打印读取到的数据
		}

		not_found(client);					// 提示网页不存在（显示404网页）
	}
	// 访问成功
	else {
		printf("服务器访问资源成功！\n");
		// 判断路径是文件还是目录
		// 按位与&，S_IFMT 来表示文件的类型，按位操作的结果就可以判断它是文件 or 目录。
		// 如果是目录
		if ((status.st_mode & S_IFMT) == S_IFDIR) {
			strcat(path, "/index.html");	// 末尾拼接一个 /index.html
		}

		// 发送资源给浏览器（封装成函数）（发送一个合法网页）
		server_file(client, path);
	}

	closesocket(client);					// 关闭客户端套接字（会释放一定资源）

	return 0;
}

int main() {
	//unsigned short port = 0;			// 端口值为0时，自动为其临时分配一个可用的端口（也可以手动指定）。
	unsigned short port = 8000;			// 默认80端口的话，就不需要手动浏览器中输入端口，但是本人测试还是要手动输入端口，不知为何

	SOCKET server_sock = startup(&port);	// 服务器套接字
	printf("httpd 服务已经启动，正在监听 %d 端口...\n", port);

	struct sockaddr_in client_addr;				// 客户端地址
	int client_addr_len = sizeof(client_addr);	// 客户端地址长度

	// 循环等待，接受客户端访问请求
	while (1) {
		// 通过服务器端套接字阻塞式等待用户通过浏览器发起访问
		// 返回：如果接受成功，返回一个新的套接字专用于和该客户端通信，失败返回-1。
		// 参数1：服务端套接字。
		// 参数2：客户端网络地址。
		// 参数3：地址长度。
		SOCKET client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);	// 发起访问的客户端的套接字
		// 接受失败。
		if (client_sock == -1) {
			// 打印错误提示，并结束程序。
			error_die("accept");
		}

		// 使用 client_sock 对用户进行访问（交互）（收发消息...）。
		// 但收发消息不能在这里直接实现，因为 accept 是一个阻塞函数，代码是顺序执行的，只有成功接受一个客户端的访问请求后，才能接受下一个服务端访问请求，这样在阻塞期间就无法同时接受多个客户端访问。
		// 最简单的一种办法是：为每一个客户端开辟一条线程来处理它与服务端之间一对一的交互，新线程创建成功之后，服务器便又可以马上等待下一个新的访问请求，使得程序运行更加高效。
		// 线程：可以简单理解成是程序的组成模块。
		// 弄懂线程就必须先了解“进程”的概念。进程可以理解成是一个程序或者软件的完整执行过程，比如我们打开QQ就得到了一个进程，相同的程序再次运行就是另外一个进程，进程之间相互独立互不干扰。
		// 线程是进程中的一个实体，是被系统独立调度和分派的基本单位。一个进程可以拥有多个线程，但是一个线程必须有一个进程。线程自己不拥有系统资源，只有运行所必须的一些数据结构，但它可以与同属于一个进程的其它线程共享进程所拥有的全部资源，同一个进程中的多个线程可以并发执行。引用自[使用CreateThread函数创建线程](https://blog.csdn.net/u012877472/article/details/49721653)

		// 创建线程（Windows和Linux平台的创建方法可能略有不同）。
		// 返回值：线程创建成功返回新线程的句柄，调用WaitForSingleObject函数等待所创建线程的运行结束，失败返回NULL。
		// 参数1：指向SECURITY_ATTRIBUTES的指针，用于定义线程内核对象的安全属性，一般传入NULL表示使用默认设置。
		// 参数2：分配以字节数表示的线程堆栈空间大小，传入0表示使用默认大小（1MB）。
		// 参数3：指向一个线程函数地址，多个线程可以使用同一个函数地址。每个线程都有自己的线程函数，线程函数是线程具体的执行代码。
		// 参数4：传递给线程函数的参数。
		// 参数5：表示创建线程的运行状态，其中CREATE_SUSPEND表示挂起当前创建的线程（调用ResumeThread()进行调度），而0表示立即执行当前创建的进程。
		// 参数6：返回新创建的线程的ID编号，传入NULL表示不需要返回该线程ID号。
		// 注：使用_beginthreadex()更安全的创建线程，在实际项目使用中尽量使用_beginthreadex()来创建线程。
		DWORD threadId = 0;
		if (NULL == CreateThread(0, 0, accept_request, (void *)client_sock, 0, &threadId)) {
			// 打印错误提示，并结束程序。
			error_die("CreateThread");
		}
	}
	
	// 关闭服务端套接字（代码健壮性）
	closesocket(server_sock);

	getchar();
	return 0;
}