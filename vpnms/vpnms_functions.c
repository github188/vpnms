/*
 * vpnms_functions.c
 *
 *  Created on: 31.08.2009
 *      Author: Andrey Chebotarev
 *        Mail:	admin@vpnms.org
 */

#include "vpnms.h"

short received_kill_sig = 0;

void StopDaemon()
{
	unlink(PIDFILE);
	syslog (LOG_NOTICE, " stopped");
	exit(EXIT_SUCCESS);
}

struct s_vpnms_config LoadConfig()
{
	struct s_vpnms_config v_config;
	config_context_t *config_f;

	config_f = config_open(CONFIGFILE);

	//инициализируем дефолтные значения


	v_config.mysql_host = config_get_string (config_f, "mysql", "host");
	v_config.mysql_username = config_get_string (config_f, "mysql", "username");
	v_config.mysql_password = config_get_string (config_f, "mysql", "password");
	v_config.mysql_database = config_get_string (config_f, "mysql", "database");
	v_config.mysql_port = config_get_int (config_f, "mysql", "port");

	v_config.vpnms_close_console = config_get_string (config_f, "vpnms", "close_console");
	v_config.vpnms_daemon_interval = config_get_int (config_f, "vpnms", "daemon_interval");
	v_config.vpnms_network = config_get_string (config_f, "vpnms", "network");
	v_config.vpnms_netmask = config_get_string (config_f, "vpnms", "netmask");
	v_config.vpnms_altq = config_get_string (config_f, "vpnms", "altq");
	v_config.vpnms_transparent_proxy = config_get_string (config_f, "vpnms", "transparent_proxy");
	v_config.vpnms_transparent_proxy_port = config_get_int (config_f, "vpnms", "transparent_proxy_port");
	v_config.vpnms_hourly_stat = config_get_string (config_f, "vpnms", "hourly_stat");
	v_config.vpnms_sql_debug = config_get_string (config_f, "vpnms", "sql_debug");
	v_config.vpnms_cmd_debug = config_get_string (config_f, "vpnms", "cmd_debug");

	v_config.vars_pfctl = config_get_string (config_f, "vars", "pfctl");
	v_config.vars_echo = config_get_string (config_f, "vars", "echo");
	v_config.vars_ond = config_get_string (config_f, "vars", "ond");

	return v_config;
}

/*
 *  Проверка демона, если запущен - возвращает 1, если нет 0.
 *  Вначале пытаемся открыть пид-файл, если его нет, то демон не запущен.
 *  Затем пытаемя заблокировать файл, если смогли, то демон не запущен.
 */

int check_daemon()
{
	int pidfd = open (PIDFILE, O_WRONLY, 0640);

	if (pidfd == -1)
    	return 0;

	if ( lockf(pidfd, F_TLOCK, 0) == 0)
    {
    	lockf(pidfd, F_ULOCK, 0);
    	close(pidfd);
    	return 0;
    }

    return 1;
}

MYSQL_RES *exec_query(char *query)
{
	MYSQL_RES	*res;
	MYSQL 		mysql;

	if ( 0 == strcasecmp (vpnms_config.vpnms_sql_debug, "yes") )
		syslog (LOG_DEBUG, "SQL: %s", query);

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, vpnms_config.mysql_host, vpnms_config.mysql_username, vpnms_config.mysql_password,
			vpnms_config.mysql_database, vpnms_config.mysql_port, NULL, 0))
	{
		syslog (LOG_ERR, "Failed to connect to database: Error: %s\n", mysql_error(&mysql));
		exit(EXIT_FAILURE);
	}

    if ( mysql_real_query(&mysql, query, strlen(query)) )
    {
		syslog (LOG_ERR, "Query failed: Error: %s\n", mysql_error(&mysql));
		exit(EXIT_FAILURE);
	}
    free(query);

    res = mysql_store_result(&mysql);

    mysql_close(&mysql);

    return res;
}

int exec_cmd(char *cmd)
{
	if ( 0 == strcasecmp (vpnms_config.vpnms_cmd_debug, "yes") )
		syslog (LOG_DEBUG, "CMD: %s", cmd);

	system(cmd);
	free(cmd);
}

char *username_by_ip(char *ip)
{
	char 		*query;
	MYSQL_RES	*res;
	MYSQL_ROW	row;
	char		*username;

	query = malloc(256);
	sprintf(query, "SELECT radreply.UserName FROM `radreply` WHERE `Attribute` = 'Framed-IP-Address' AND `Value` = '%s' ", ip);

    res = exec_query(query);

    if(mysql_num_rows(res) < 1)
    	return NULL;

    row = mysql_fetch_row(res);
    username = strdup(row[0]);
    mysql_free_result(res);

    return username;
}

char *ip_by_username(char *username)
{
	char 		*query;
	MYSQL_RES	*res;
	MYSQL_ROW	row;
	char		*ip;

	query = malloc(256);
	sprintf(query, "SELECT radreply.Value FROM `radreply` WHERE `Attribute` = 'Framed-IP-Address' AND `UserName` = '%s'", username);

    res = exec_query(query);

    if(mysql_num_rows(res) < 1)
    	return NULL;

    row = mysql_fetch_row(res);
    ip = strdup(row[0]);
    mysql_free_result(res);

    return ip;
}

char *check_status(char *username)
{
	char 		*query;
	MYSQL_RES	*res;
	MYSQL_ROW	row;
	char		*status;

	query = malloc(256);
	sprintf(query, "SELECT radcheck.status FROM `radcheck` WHERE `UserName` = '%s'", username);

    res = exec_query(query);
    if (mysql_num_rows(res) < 1)
    	return NULL;

    row = mysql_fetch_row(res);
    status = strdup(row[0]);

    mysql_free_result(res);

    return status;
}

long long int get_sess_id(char *username)
{
	char 		*query;
	MYSQL_RES	*res;
	MYSQL_ROW	row;
	char		*id;
	myint		SessId;

	query = malloc(256);
	sprintf(query, "SELECT MAX(SessId) FROM `sessions` WHERE `UserName` = '%s'", username);

    res = exec_query(query);
    row = mysql_fetch_row(res);

    if ( row[0] == NULL)
    	return -1;

    SessId = atoll(row[0]);
    mysql_free_result(res);

    return SessId;
}

struct s_balance check_balance(char *username)
{
	struct s_balance	balance;
	char 				*query;
	MYSQL_RES			*res;
	MYSQL_ROW			row;
	myint				bytes;

	//limit
	query = malloc(256);
	sprintf(query, "SELECT radcheck.limit,radcheck.bonus,radcheck.out_limit,radcheck.limit_type FROM `radcheck` WHERE `UserName` = '%s'", username);
	res = exec_query(query);
	if (mysql_num_rows(res) != 1)
	{
		syslog (LOG_ERR, "Error: in function check_balance(), check the database for logical errors.\n");
		exit(EXIT_FAILURE);
	}
	row = mysql_fetch_row(res);
	balance.limit = atoll(row[0]) + atoll(row[1]);
	balance.out_limit = (balance.limit/100) * atoi(row[2]);
	balance.limit_type = strdup(row[3]);
	mysql_free_result(res);


	//input, output, local input, local output
	query = malloc(256);
	sprintf(query, "SELECT SUM(InternetIn) AS Input, SUM(InternetOut) AS Output, SUM(LocalIn) AS LocalInput, SUM(LocalOut) AS LocalOutput "
			"FROM `sessions` WHERE `UserName` = '%s'", username);
	res = exec_query(query);
	row = mysql_fetch_row(res);

	if (row[0] == NULL)
		bytes = 0;
	else
		bytes = atoll(row[0]);
	balance.input = bytes;

	if (row[1] == NULL)
		bytes = 0;
	else
		bytes = atoll(row[1]);
	balance.output = bytes;

	if (row[2] == NULL)
		bytes = 0;
	else
		bytes = atoll(row[2]);
	balance.local_input = bytes;

	if (row[4] == NULL)
		bytes = 0;
	else
		bytes = atoll(row[4]);
	balance.local_output = bytes;

	mysql_free_result(res);

	return balance;
}

/*
 * Очищает все правила маршрутизации и проксирования
 * для заданного пользователя.
 */

int clear_rules(char *username)
{
	char *cmd;

	cmd = malloc(512);
	sprintf(cmd, "%s -a \"%s/%s\" -F rules", vpnms_config.vars_pfctl, PF_VPNMS_ANCHOR, username);
	exec_cmd(cmd);

	cmd = malloc(512);
	sprintf(cmd, "%s -a \"%s/%s\" -F nat", vpnms_config.vars_pfctl, PF_VPNMSP_ANCHOR, username);
	exec_cmd(cmd);

	cmd = malloc(512);
	sprintf(cmd, "%s -k %s", vpnms_config.vars_pfctl, ip_by_username(username));
	exec_cmd(cmd);

	return 0;
}

/*
 *  Добавяет правила маршрутизации и проксирования
 *  в зависимости от статуса.
 */

int add_rules(char *username, char *if_name)
{
	char *cmd;
	char *status;
	char *ip;
	char *query;
	char *udp_ports;
	char *tcp_ports;
	char *bandwidth;
	char *bandwidth_id;
	char *bandwidth_str;
	char *test;
	char *echo_path;
	char *pfctl_path;
	char *local_subnets_pf_str;
	char * pString;
	unsigned int proxy_port;
	unsigned int rows;
	myint nSize;
	MYSQL_RES	*res;
	MYSQL_ROW	row;

	int add_rules_debug = 1;

	status = check_status(username);
	if ( status == NULL )
		return 1;

	ip = ip_by_username(username);
	if ( ip == NULL )
		return 1;

	//выбираем список разрешенных портов
	query = malloc(256);
	sprintf(query, "SELECT radcheck.allow_tcp_port,radcheck.allow_udp_port FROM `radcheck` WHERE `UserName` = '%s'", username);
	res = exec_query(query);
    if (mysql_num_rows(res) < 1)
    {
    	syslog (LOG_ERR, " in function add_rules(): can't read allowed ports, user %s not found", username);
    	exit(EXIT_FAILURE);
    }
	row = mysql_fetch_row(res);
	tcp_ports = strdup(row[0]);
	udp_ports = strdup(row[1]);
	mysql_free_result(res);

	if (strcasecmp(vpnms_config.vpnms_altq, "yes") == 0)
	{
		//выбираем скорость канала
		query = malloc(256);
		sprintf(query, "SELECT radcheck.bandwidth FROM `radcheck` WHERE `UserName` = '%s'", username);
		res = exec_query(query);
	    if (mysql_num_rows(res) < 1)
	    {
	    	syslog (LOG_ERR, " in function add_rules(): can't read bandwidth id, user %s not found", username);
	    	exit(EXIT_FAILURE);
	    }
		row = mysql_fetch_row(res);
		bandwidth_id = strdup(row[0]);
		mysql_free_result(res);

		query = malloc(256);
		sprintf(query, "SELECT bandwidth.bandwidth_name FROM `bandwidth` WHERE `bw_id` = '%s'", bandwidth_id);
		res = exec_query(query);
	    if (mysql_num_rows(res) < 1)
	    {
	    	syslog (LOG_ERR, " in function add_rules(): can't read bandwidth name, bandwidth id %s not found", bandwidth_id);
	    	exit(EXIT_FAILURE);
	    }

	    row = mysql_fetch_row(res);
	    bandwidth = strdup(row[0]);
	    mysql_free_result(res);
	    bandwidth_str = malloc(10 + strlen(bandwidth));
	    sprintf(bandwidth_str, " queue %s", bandwidth);

		free(bandwidth);
		free(bandwidth_id);
	}
	else
		bandwidth_str = "";

	/*
	 * почему-то в sprintf использование vpnms_config иногда выпадает в segmentation fault
	 * поэтому вот такое извращение
	 */

	proxy_port = vpnms_config.vpnms_transparent_proxy_port;
	pfctl_path = vpnms_config.vars_pfctl;
	echo_path = vpnms_config.vars_echo;

	if ( strcasecmp(status, STATUS_BLOKED) == 0 )
	{
		//ничего не делаем
	}
	else if ( strcasecmp(status, STATUS_WORKING) == 0 )
	{
		//если открыты все порты
		if ( (strcasecmp(tcp_ports, "*") == 0) && (strcasecmp(udp_ports, "*") == 0) )
		{
			cmd = malloc(512);
			sprintf(cmd, "%s \"pass in from %s to any%s\" | %s -a \"vpnms/%s\" -f -",
					echo_path, ip, bandwidth_str, pfctl_path, username);
			exec_cmd(cmd);

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}
		}
		//если открыты все tcp порты, а udp не все
		else if  (strcasecmp(tcp_ports, "*") == 0)
		{
			cmd = malloc(512);
			sprintf(cmd, "%s \"pass in proto tcp from %s to any%s\" | %s -a \"vpnms/%s\" -f -",
					echo_path, ip, bandwidth_str, pfctl_path, username);
			exec_cmd(cmd);

			//если udp-порты не запрещены
			if (strcasecmp(udp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto udp from %s to any port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, udp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}
		}
		//если открыты все udp порты, а tcp не все
		else if  (strcasecmp(udp_ports, "*") == 0)
		{
			cmd = malloc(512);
			sprintf(cmd, "%s \"pass in proto udp from %s to any%s\" | %s -a \"vpnms/%s\" -f -",
					echo_path, ip, bandwidth_str, pfctl_path, username);
			exec_cmd(cmd);

			//если tcp-порты не запрещены
			if (strcasecmp(tcp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto tcp from %s to any port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, tcp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}
		}
		//если открыты только определенные порты
		else if ( (strcasecmp(tcp_ports, "*") != 0) && (strcasecmp(udp_ports, "*") != 0) )
		{
			//если tcp-порты не запрещены
			if (strcasecmp(tcp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto tcp from %s to any port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, tcp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если udp-порты не запрещены
			if (strcasecmp(udp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto udp from %s to any port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, udp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}
		}

	}
	else if ( (strcasecmp(status, STATUS_LIMIT_EXPIRE) == 0) || (strcasecmp(status, STATUS_LOCAL_ONLY) == 0) )
	{

		//строим список локальных адресов
        query = malloc(256);
        sprintf(query, "SELECT Subnet_Address, Masklength FROM `subnets`");

        res = exec_query(query);
        rows = mysql_num_rows(res);

        //считаем сколько выделить памяти
        nSize = rows*22;
        //выделяем память нужного размера
        local_subnets_pf_str = malloc( nSize );
        //копируем указатель на начало памяти, в local_subnets_pf_str остался указатель на начало
        pString = local_subnets_pf_str;
        //заполняем нулями память
        memset( local_subnets_pf_str, 0, nSize );

        sprintf(pString, "{");
        //перемещаем указатель на нужное кол-во эелементов в памяти вперед
        pString += 1;

        while ((row = mysql_fetch_row(res)))
        {
        	if ( 0 == strcasecmp(row[1], "32") )
        	{
        		sprintf( pString, " %s", row[0] );
        		pString += strlen( row[0] ) + 1;
        	}
        	else
        	{
        		sprintf(pString, " %s/%s", row[0], row[1]);
        		pString += strlen( row[0] ) + 1 + strlen( row[1] ) + 1;
        	}
        }
        mysql_free_result(res);

        sprintf(pString, " }");
        //--------------------

		//если открыты все порты
		if ( (strcasecmp(tcp_ports, "*") == 0) && (strcasecmp(udp_ports, "*") == 0) )
		{
			cmd = malloc(512);
			sprintf(cmd, "%s \"pass in from %s to %s%s\" | %s -a \"vpnms/%s\" -f -",
					echo_path, ip, local_subnets_pf_str, bandwidth_str, pfctl_path, username);
			exec_cmd(cmd);

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}
		}
		//если открыты все tcp порты, а udp не все
		else if  (strcasecmp(tcp_ports, "*") == 0)
		{
			cmd = malloc(512);
			sprintf(cmd, "%s \"pass in proto tcp from %s to %s%s\" | %s -a \"vpnms/%s\" -f -",
					echo_path, ip, local_subnets_pf_str, bandwidth_str, pfctl_path, username);
			exec_cmd(cmd);

			//если udp-порты не запрещены
			if (strcasecmp(udp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto udp from %s to %s port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, local_subnets_pf_str, udp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}

		}
		//если открыты все udp порты, а tcp не все
		else if  (strcasecmp(udp_ports, "*") == 0)
		{
			cmd = malloc(512);
			sprintf(cmd, "%s \"pass in proto udp from %s to %s%s\" | %s -a \"vpnms/%s\" -f -",
					echo_path, ip, local_subnets_pf_str, bandwidth_str, pfctl_path, username);
			exec_cmd(cmd);

			//если tcp-порты не запрещены
			if (strcasecmp(tcp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto tcp from %s to %s port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, local_subnets_pf_str, tcp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}

		}
		//если открыты только определенные порты
		else if ( (strcasecmp(tcp_ports, "*") != 0) && (strcasecmp(udp_ports, "*") != 0) )
		{
			//если tcp-порты не запрещены
			if (strcasecmp(tcp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto tcp from %s to %s port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, local_subnets_pf_str, tcp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если udp-порты не запрещены
			if (strcasecmp(udp_ports, "-") != 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"pass in proto udp from %s to %s port %s%s\" | %s -a \"vpnms/%s\" -f -",
						echo_path, ip, local_subnets_pf_str, udp_ports, bandwidth_str, pfctl_path, username);
				exec_cmd(cmd);
			}

			//если используется прозрачный прокси, то включаем перенаправление
			if (strcasecmp(vpnms_config.vpnms_transparent_proxy, "yes") == 0)
			{
				cmd = malloc(512);
				sprintf(cmd, "%s \"rdr on %s inet proto tcp from %s to any port www -> 127.0.0.1 port %u\" | %s -a \"vpnmsp/%s\" -f -",
						echo_path, if_name, ip, proxy_port, pfctl_path, username);
				exec_cmd(cmd);
			}
		}
        free(local_subnets_pf_str);
	}

	//освобождаем память
	free(tcp_ports);
	free(udp_ports);

	return 0;
}
