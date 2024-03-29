#!/usr/bin/php
<?php
$db_conf_file=$_SERVER['HOME'].'/.my.cnf';
$delimiter = ";";
$default_db = "lockdebugging";
$default_port = 3306;

$options = getopt("d:f:m:a:i:c:l:w:o");
if ($options == FALSE ||
    !array_key_exists('f',$options) || strlen(trim($options['f'])) == 0 ||
    !array_key_exists('d',$options) || strlen(trim($options['d'])) == 0) {
	die(usage($argv[0])."\n");
}

if (array_key_exists('l',$options)) {
	$db_conf_file = $options['l'];
}
$db_conf = parse_ini_file($db_conf_file,true);
if (!$db_conf) {
	die("Cannot read db conf file: " . $db_conf_file . "\n");
}

if (array_key_exists('m',$options)) {
	$member_filter = $options['m'];
} else {
	$member_filter = null;
}

if (array_key_exists('a',$options)) {
	$ac_type_filter = $options['a'];
} else {
	$ac_type_filter = null;
}

if (array_key_exists('c',$options)) {
	$context_filter = $options['c'];
} else {
	$context_filter = null;
}

if (array_key_exists('i',$options)) {
	$instance_filter = $options['i'];
} else {
	$instance_filter = null;
}

if (array_key_exists('o',$options)) {
	$own_lock_filter = "(l.embedded_in IS NULL OR l.embedded_in = alloc_id)";
} else {
	$own_lock_filter = "1";
}

if (array_key_exists('p',$options)) {
	$db_conf['client']['port'] = $options['p'];
} else if (isset($db_conf['client']['port'])) {
	fwrite(STDERR,"Using port specified in configfile.\n");
} else {
	fwrite(STDERR,"Using default port: " . $default_port . "\n");
	$db_conf['client']['port'] = $default_port;
}

if (array_key_exists('w',$options)) {
	$db_conf['client']['database'] = $options['w'];
} else if (isset($db_conf['client']['database'])) {
	fwrite(STDERR,"Using database specified in configfile.\n");
} else {
	fwrite(STDERR,"Using default database: " . $default_db . "\n");
	$db_conf['client']['database'] = $default_db;
}

$outfile_name = $options['f'];
$datatype = $options['d'];

$sql = new mysqli;
$sql->init();
//$sql->options(MYSQLI_READ_DEFAULT_FILE,$db_conf_file);
//$sql->options(MYSQLI_READ_DEFAULT_GROUP,'client');
$contexts = array("unknown" => "lh.start IS NULL",
		  "hardirq" => "(lh.lastPreemptCount & 0xf0000)",
		  "softirq" => "(lh.lastPreemptCount & 0x0ff00)",
//		  "hsirq"   => "(lh.lastPreemptCount & 0xfff00)",
		  "noirq"   => "((lh.lastPreemptCount & 0xfff00) = 0)");
$ac_types = array("r", "w");

if (!isset($db_conf['client']['database'])) {
	$db_conf['client']['database'] = $default_db;
}

fwrite(STDERR,"Connecting to \"" . $db_conf['client']['user'] . "@" . $db_conf['client']['host'] . ":" . $db_conf['client']['port'] . "\". Using db \"" . $db_conf['client']['database'] . "\".\n");
$sql->real_connect($db_conf['client']['host'],$db_conf['client']['user'],$db_conf['client']['password'],$db_conf['client']['database'],$db_conf['client']['port']);
if ($sql->connect_errno) {
	die($sql->connect_error . "\n");
}

$query  = sprintf("SELECT id FROM data_types WHERE name = '%s';",$sql->real_escape_string($datatype));
if ($sql->real_query($query)) {
	$result = $sql->store_result();
	if ($result && $result->num_rows == 1) {
		list($datatype_id) = $result->fetch_array();
	} else {
		if ($result) {
			$result->free_result();
		}
		$sql->close();
		die("Cannot find an id for the given datatype:" . $datatype . "\n");
	}
} else {
	$sql->close();
	die("Cannot find an id for the given datatype:" . $datatype . "\n");
}

if (!is_null($context_filter)) {
		if (isset($contexts[$context_filter])) {
			$context_clause = $contexts[$context_filter];
		} else {
			$sql->close();
			die("No such context!\n");
		}
} else {
		$context_clause = "1";
}
if (!is_null($ac_type_filter)) {
		if (in_array($ac_type_filter,$ac_types)) {
			$ac_type_clause = "'" . $ac_type_filter . "'";
		} else {
			$sql->close();
			die("No such ac type!\n");
		}
} else {
		$ac_type_clause = "1";
}

if (is_null($member_filter)) {
	$member_clause = "1";
} else {
	$member_clause = sprintf("sl.member = '%s'",$sql->real_escape_string($member_filter));
}
if (is_null($instance_filter)) {
	$instance_clause = "1";
} else {
	$instance_clause = sprintf("a.id = %d",$sql->real_escape_string($instance_filter));
}

$dist_query = "
SELECT
	ac_type,
	dt_name,
	sl_member,
	lock_member,
	context,
	db,
	COUNT(*) AS num
FROM
(
	SELECT -- DISTINCT
		ac_id,
		ac_type,
		dt_name,
		sl_member,
		IF(l.type IS NULL, 'null',
		  IF(sl2.member IS NULL,CONCAT('global_',l.type,'_',l.id),CONCAT(sl2.member,'_',IF(l.embedded_in = alloc_id,'1','0')))) AS lock_member,
		CASE 
			WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
			WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
			WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
			ELSE 'unknown'
		END AS context,
		'".$db_conf['client']['database'] ."' AS db
	FROM
	(
		SELECT
			ac.id AS ac_id,
			ac.txn_id AS ac_txn_id,
			ac.alloc_id AS alloc_id,
			ac.type AS ac_type,
			sl.member AS sl_member,
			dt.name AS dt_name
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		INNER JOIN data_types AS dt ON dt.id=a.type
		LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
		LEFT JOIN blacklist bl ON bl.datatype_id = a.type AND bl.fn = ac.fn AND (bl.datatype_member IS NULL OR bl.datatype_member = sl.member)
		WHERE 
			a.type = ".$datatype_id." AND
			".$member_clause." AND
			".$ac_type_clause." AND
			".$instance_clause." AND
			bl.fn IS NULL
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id 
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
	WHERE
		".$own_lock_filter." AND
		".$context_clause."
) u
GROUP BY ac_type, lock_member, sl_member, context
ORDER BY num DESC;";

fwrite(STDERR,$dist_query."\n");
if (strcmp($outfile_name ,"--") == 0) {
	$outfile = STDOUT;
} else {
	$outfile = fopen($outfile_name,"w+");
}
if ($outfile === false) {
	$sql->close();
	die("Cannot open " . $outfile_name . "\n");
}
$line = "ac_type" . $delimiter . "lock_member" . $delimiter . "member";
$line .= $delimiter . "context" . $delimiter . "num" . $delimiter . "db" . $delimiter . "dt_name\n";
fwrite($outfile,$line);

$start = time();
if ($sql->multi_query($dist_query)) {
	do {
		if ($result = $sql->store_result()) {
			while ($row = $result->fetch_assoc()) {
				$line = $row['ac_type'] . $delimiter . $row['lock_member'] . $delimiter . $row['sl_member'] . $delimiter . $row['context'] ;
				$line .= $delimiter . $row['num'] . $delimiter . $row['db'] . $delimiter . $row['dt_name'] . "\n";
				fwrite($outfile,$line);
			}
			$result->free_result();
		}
		if ($sql->more_results() && strcmp($outfile_name,"/dev/stdout") == 0) {
			fwrite(STDERR,"-----------------\n");
		}
	} while ($sql->next_result());
	$end = time();
	fwrite(STDERR,"Query took " . ($end - $start) . " seconds\n");
} else {
	fwrite(STDERR,"Cannot retrieve lock information: " . $sql->error . "\n");
}

$sql->close();
fclose($outfile);

function usage($name) {
	fwrite(STDERR,"$name [-l <my.cnf>] [-w <database>] [-o (use global and own locks only)] [-p <port>] [-i <instance id>] [-a <access type, r or w>] [-m <member>] [-c <context, e.g. noirq, or hardirq>] -d <datatype> -f <output file>\n");
}
?>
