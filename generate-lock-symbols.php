#!/usr/bin/php
<?php
$db_name = 'lockdebugging';
$db_user = 'al';
$db_passwd = 'howaih1S';
$db_server = '129.217.43.116';
$delimiter = ';';
$delimiter_locks = '+';

if (count($argv) < 2) {
        die(usage($argv[0])."\n");
}

if (count($argv) > 2 && $argv[1] == '-d') {
	$debug = 1;
	$outfile_name = $argv[2];
} else {
	$debug = 0;
	$outfile_name = $argv[1];
}


$db_link = mysqli_connect($GLOBALS['db_server'],$GLOBALS['db_user'],$GLOBALS['db_passwd']) OR die(mysqli_error());
mysqli_select_db($db_link,$GLOBALS['db_name']) OR die(mysqli_error());

$query = "SELECT ac.id AS ac_id, ac.alloc_id,ac.type,a.type AS data_type,
		 lh.lock_id AS locks, l.type AS lock_types, a2.type AS embedded_in,
		 ac.address - a.ptr AS offset, ac.size
	  FROM accesses AS ac
	  INNER JOIN allocations AS a ON a.id=ac.alloc_id
	  INNER JOIN locks_held AS lh ON lh.access_id=ac.id
	  INNER JOIN locks AS l ON l.id=lh.lock_id
	  LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	  ORDER BY ac.id
	  -- LIMIT 0,100";
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

$line .= "alloc_id" . $delimiter . "type" . $delimiter . "data_type" . $delimiter . "locks" . $delimiter . "lock_types" . $delimiter . "embedded_in" . $delimiter . "offset" . $delimiter . "size\n";
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
	$locks = array();
	$lock_types = array();
	$embedded_in = array();

	do {
		$locks[] = $row['locks'];
		$lock_types[] = $row['lock_types'];
		if (is_null($row['embedded_in'])) {
			$embedded_in[] = "null";
		} else {
			$embedded_in[] = $row['embedded_in'];
		}
		$row = mysqli_fetch_assoc($result);
		$i++;
	} while ($row && $ac_id == $row['ac_id']);

	if ($debug) {
		$line = $ac_id . $delimiter;
	} else {
		$line = "";
	}
	$line .= $alloc_id . $delimiter . $type . $delimiter . $data_type . $delimiter . implode($delimiter_locks,$locks) . $delimiter . implode($delimiter_locks,$lock_types) . $delimiter . implode($delimiter_locks,$embedded_in) . $delimiter . $offset . $delimiter . $size . "\n";
	$k++;
	fwrite($outfile,$line);
}

echo "Processed $i lines\n";
echo "Created $k lines\n";
fclose($outfile);
mysqli_close($db_link);

function usage($name) {
	echo "$name <output file>\n";
}
?>
