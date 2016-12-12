#!/usr/bin/php
<?php
$db_conf_file='/fs/scratch/al/coccinelle/tools/my.cnf';
$delimiter = ";";

$options = getopt("d:f:m:a:i:");
if ($options == FALSE ||
    !array_key_exists('f',$options) || strlen(trim($options['f'])) == 0 ||
    !array_key_exists('d',$options) || strlen(trim($options['d'])) == 0) {
	die(usage($argv[0])."\n");
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

$outfile_name = $options['f'];
$datatype = $options['d']; 


$sql = new mysqli;
$sql->init();
//$sql->options(MYSQLI_READ_DEFAULT_FILE,$db_conf_file);
//$sql->options(MYSQLI_READ_DEFAULT_GROUP,'client');
$db_conf = parse_ini_file($db_conf_file,true);
$contexts = array("unknown" => "lh.start IS NULL",						// 
		  "hardirq" => "lh.start IS NOT NULL AND lh.lastPreemptCount & 0xf0000",
		  "softirq" => "lh.start IS NOT NULL AND lh.lastPreemptCount & 0x0ff00",
		  "hsirq"   => "lh.start IS NOT NULL AND lh.lastPreemptCount & 0xfff00",
		  "noirq"   => "lh.start IS NOT NULL AND (lh.lastPreemptCount & 0xfff00) = 0");
/*		  "hardirq" => "lh.start IS NOT NULL AND (l.embedded_in = alloc_id OR l.type = 'rcu') AND lh.lastPreemptCount & 0xf0000",
		  "softirq" => "lh.start IS NOT NULL AND (l.embedded_in = alloc_id OR l.type = 'rcu') AND lh.lastPreemptCount & 0x0ff00",
		  "hsirq"   => "lh.start IS NOT NULL AND (l.embedded_in = alloc_id OR l.type = 'rcu') AND lh.lastPreemptCount & 0xfff00",
		  "noirq"   => "lh.start IS NOT NULL AND (l.embedded_in = alloc_id OR l.type = 'rcu') AND (lh.lastPreemptCount & 0xfff00) = 0");*/
$ac_types = array("r", "w");

$dist_query_raw = "SELECT
	ac_type,
	locks,
	lock_types,
	sl_member,
	embedded_in_same,
	'%s' AS context,
	COUNT(*) AS num
FROM
(
	SELECT
		ac_id, alloc_id, ac_type, ac_fn, ac_address, a_ptr, sl_member,
		GROUP_CONCAT(IFNULL(lh.lock_id,\"null\") ORDER BY l.id ASC SEPARATOR '+') AS locks,
		GROUP_CONCAT(IFNULL(l.type,\"null\") ORDER BY l.id ASC SEPARATOR '+') AS lock_types,
		GROUP_CONCAT(IF(l.embedded_in = alloc_id,'1','0') ORDER BY l.id ASC SEPARATOR '+') AS embedded_in_same
	FROM
	(
		SELECT
			ac.id AS ac_id, 
			ac.alloc_id AS alloc_id,
			ac.type AS ac_type,
			ac.fn AS ac_fn,
			ac.address AS ac_address,
			a.ptr AS a_ptr,
			sl.member AS sl_member
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
		WHERE 
			%s AND ac.type ='%s' AND %s
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.access_id=ac_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	WHERE
		%s
	GROUP BY ac_id
) t
GROUP BY ac_type, lock_types, sl_member
ORDER BY num DESC;";


$sql->real_connect($db_conf['client']['host'],$db_conf['client']['user'],$db_conf['client']['password'],$db_conf['client']['database']);
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

$dist_query = "";
/*$query  = sprintf("SELECT * FROM structs_layout WHERE type_id = %d;",$datatype_id);
if ($sql->real_query($query)) {
	$result = $sql->store_result();
	if ($result ) {
		while ($row = $result->fetch_assoc()) {
			foreach ($ac_types AS $ac_type) {
				foreach ($contexts AS $id => $value) {
					$dist_query .= sprintf($dist_query_raw,$id, $row['member'],$ac_type,$value) . "\n";
				}
			}
		}
		$result->free_result();
	} else {
		if ($result) {
			$result->free_result();
		}
		$sql->close();
		die("Cannot retrieve member for data type: " . $datatype . "(" . $datatype_id . ")\n");
	}
} else {
	$error_msg = $sql->error;
	$sql->close();
	die("Cannot retrieve member for data type " . $datatype . "(" . $datatype_id . "):" . $error_msg . "\n");
}*/

foreach ($ac_types AS $ac_type) {
	if (!is_null($ac_type_filter) && strcmp($ac_type,$ac_type_filter) != 0) {
		continue;
	}
	foreach ($contexts AS $id => $value) {
		if (!is_null($context_filter) && strcmp($value,$context_filter) != 0) {
			continue;
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
		$dist_query .= sprintf($dist_query_raw, $id, $member_clause, $ac_type, $instance_clause, $value) . "\n";
	}
}

$outfile = fopen($outfile_name,"w+");
if ($outfile === false) {
	$sql->close();
	die("Cannot open " . $outfile_name . "\n");
}
$line = "ac_type" . $delimiter . "locks" . $delimiter . "lock_types" . $delimiter . "embedded_in_same" . $delimiter . "member" . $delimiter . "context" . $delimiter . "num\n";
fwrite($outfile,$line);
//echo $dist_query;

if ($sql->multi_query($dist_query)) {
	do {
		if ($result = $sql->store_result()) {
			while ($row = $result->fetch_assoc()) {
				$line = $row['ac_type'] . $delimiter . $row['locks'] . $delimiter . $row['lock_types'];
				$line .= $delimiter . $row['embedded_in_same'] . $delimiter . $row['sl_member'] . $delimiter . $row['context'];
				$line .= $delimiter . $row['num'] . "\n";
				fwrite($outfile,$line);
			}
			$result->free_result();
		}
		if ($sql->more_results() && strcmp($outfile_name,"/dev/stdou") == 0) {
			printf("-----------------\n");
		}
	} while ($sql->next_result());
} else {
	echo "Cannot retrieve lock information: " . $sql->error . "\n";
}

$sql->close();
fclose($outfile);

function usage($name) {
	echo "$name -d <datatype> -f <output file>\n";
}
?>
