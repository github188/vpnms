/*
 * monthlyreset.c
 *
 *  Created on: 24.02.2010
 *      Author: metallic
 */

#include "vpnms.h"

int main (int argc, char **argv)
{
	char	*cmd;
	char	*query;

	if(argv[1] == NULL)
	{
		syslog (LOG_ERR, " no proof option");
        exit(EXIT_FAILURE);
	}

	if(0 != strcasecmp (argv[1], "confirm"))
	{
		syslog (LOG_ERR, " no proof option");
        exit(EXIT_FAILURE);
	}

	//loading config
    vpnms_config = LoadConfig();

	//останавливаем mpd
	cmd = malloc(512);
	sprintf(cmd, "%s stop", vpnms_config.vars_mpd_rc_script);
	exec_cmd(cmd);

	sleep(10);

	//ротация сессий
	query = malloc(256);
	sprintf(query, "DELETE FROM `sessions` WHERE `Rotation` = '3'");
	exec_query(query);

	query = malloc(256);
	sprintf(query, "UPDATE `sessions` SET `Rotation` = '3' WHERE `Rotation` = '2'");
	exec_query(query);

	query = malloc(256);
	sprintf(query, "UPDATE `sessions` SET `Rotation` = '2' WHERE `Rotation` = '1'");
	exec_query(query);

	//ротация почасовой статистики
	query = malloc(256);
	sprintf(query, "DELETE FROM `hourlystat` WHERE `rotation` = '3'");
	exec_query(query);

	query = malloc(256);
	sprintf(query, "UPDATE `hourlystat` SET `Rotation` = '3' WHERE `rotation` = '2'");
	exec_query(query);

	query = malloc(256);
	sprintf(query, "UPDATE `hourlystat` SET `Rotation` = '2' WHERE `rotation` = '1'");
	exec_query(query);

	//обнуляем бонусы
	query = malloc(128);
	sprintf(query, "UPDATE `radcheck` SET `bonus` = 0");
	exec_query(query);

	//обновляем статус пользователей, у которых статус "лимит исчерпан"
	query = malloc(128);
	sprintf(query, "UPDATE `radcheck` SET `status` = '%s' WHERE `status` = '%s'", STATUS_WORKING, STATUS_LIMIT_EXPIRE);
	exec_query(query);

	//запускаем mpd
	cmd = malloc(512);
	sprintf(cmd, "%s start", vpnms_config.vars_mpd_rc_script);
	exec_cmd(cmd);

	return 0;
}
