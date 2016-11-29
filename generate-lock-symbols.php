#!/usr/bin/php
<?php
$db_name = 'lockdebugging';
$db_user = 'al';
$db_passwd = 'howaih1S';
$db_server = '129.217.43.116';
$delimiter = ';';
$delimiter_locks = '+';

$options = getopt("dlf:");
if ($options == FALSE ||
    !array_key_exists('f',$options) ||
    strlen(trim($options['f'])) == 0) {
	die(usage($argv[0])."\n");
}

$debug = array_key_exists('d',$options);
$lazy = array_key_exists('l',$options);
$outfile_name = $options['f'];

$db_link = mysqli_connect($GLOBALS['db_server'],$GLOBALS['db_user'],$GLOBALS['db_passwd']) OR die(mysqli_error());
mysqli_select_db($db_link,$GLOBALS['db_name']) OR die(mysqli_error());

if ($lazy) {
	$join_type = "LEFT";
} else {
	$join_type = "INNER";
}

$query = "SELECT ac.id AS ac_id, ac.fn AS ac_fn, ac.alloc_id,ac.type,a.type AS data_type,
		 lh.lock_id AS locks, l.type AS lock_types, a2.type AS embedded_in_type,
		 IF(l.embedded_in = a.id,'1','0') AS embedded_in_same,
		 ac.address - a.ptr AS offset, ac.size, sl.member, sl.offset AS member_offset,
		 IF(lh.lastFile IS NULL,NULL,CONCAT_WS(':',lh.lastFile,lh.lastLine,lh.lastFn)) AS pos,
		 HEX(lh.lastPreemptCount) AS  lastPreemptCount
	  FROM accesses AS ac
	  INNER JOIN allocations AS a ON a.id=ac.alloc_id
	  LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
	  $join_type JOIN locks_held AS lh ON lh.access_id=ac.id
	  $join_type JOIN locks AS l ON l.id=lh.lock_id
	  LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	  ORDER BY ac.id
	  -- LIMIT 0,100";
if ($debug) {
	echo "Executing query:\n". $query ."\n";
}
$start = time();
$result = mysqli_query($db_link,$query,MYSQLI_USE_RESULT);
if ($result === false) {
	echo "query failed\n".mysqli_error($db_link)."\n";
	die();
}
$end = time();
echo "Query took " . ($end - $start) . " seconds\n";

$outfile = fopen($outfile_name,"w+");
if ($outfile === false) {
	mysqli_close($db_link);
	die("Cannot open " . $outfile_name . "\n");
}

if ($debug) {
	$line = "ac_id" . $delimiter;
} else {
	$line = "";
}

$line .= "alloc_id" . $delimiter . "type" . $delimiter . "data_type" . $delimiter . "locks" . $delimiter . "lock_types" . $delimiter . "embedded_in_type" .$delimiter . "embedded_in_same" . $delimiter . "offset" . $delimiter . "size" . $delimiter . "member" . $delimiter . "member_offset" . $delimiter . "ac_fn" . $delimiter ."pos" . $delimiter ."preemptcount\n";
fwrite($outfile,$line);
$i = 0;
$k = 0;

$row = mysqli_fetch_assoc($result);
while ($row) {
	$ac_id = $row['ac_id'];
	$alloc_id = $row['alloc_id'];
	$type = $row['type'];
	$data_type = $row['data_type'];
	$offset = $row['offset'];
	$size = $row['size'];
	$member = $row['member'];
	$fn = $row['ac_fn'];
	$member_offset = $row['member_offset'];
	$locks = array();
	$lock_types = array();
	$embedded_in_type = array();
	$embedded_in_same = array();
	$pos = array();
	$preemptcount = array();

	do {
		if (is_null($row['locks'])) {
			$locks[] = "null";
		} else {
			$locks[] = $row['locks'];
		}
		if (is_null($row['lock_types'])) {
			$lock_types[] = "null";
		} else {
			$lock_types[] = $row['lock_types'];
		}
		if (is_null($row['embedded_in_type'])) {
			$embedded_in_type[] = "null";
		} else {
			$embedded_in_type[] = $row['embedded_in_type'];
		}
		if (is_null($row['embedded_in_same'])) {
			$embedded_in_same[] = "null";
		} else {
			$embedded_in_same[] = $row['embedded_in_same'];
		}
		if (is_null($row['pos'])) {
			$pos[] = "null";
		} else {
			$pos[] = $row['pos'];
		}
		if (is_null($row['lastPreemptCount'])) {
			$preemptcount[] = "null";
		} else {
			$preemptcount[] = $row['lastPreemptCount'];
		}
		$row = mysqli_fetch_assoc($result);
		$i++;
	} while ($row && $ac_id == $row['ac_id']);

	if ($debug) {
		$line = $ac_id . $delimiter;
	} else {
		$line = "";
	}
	$line .= $alloc_id . $delimiter . $type . $delimiter . $data_type . $delimiter . implode($delimiter_locks,$locks) . $delimiter;
	$line .= implode($delimiter_locks,$lock_types) . $delimiter . implode($delimiter_locks,$embedded_in_type) . $delimiter;
	$line .= implode($delimiter_locks,$embedded_in_same) . $delimiter. $offset . $delimiter;
	$line .= $size . $delimiter . $member . $delimiter . $member_offset . $delimiter;
	$line .= $fn . $delimiter . implode($delimiter_locks,$pos);
	$line .= $delimiter . implode($delimiter_locks,$preemptcount) . "\n";
	$k++;
	fwrite($outfile,$line);
}

echo "Processed $i lines\n";
echo "Created $k lines\n";
fclose($outfile);
mysqli_close($db_link);

function usage($name) {
	echo "$name [-d] [-l] -f <output file>\n";
	echo "-d		Add a few more columns for debugging purpose\n";
	echo "-l		Use a left join instead of an inner. This will add memory accesses that do not have any lock held.\n";
}
?>
