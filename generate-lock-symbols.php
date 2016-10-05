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

$outfile_name = $argv[1];


$db_link = mysqli_connect($GLOBALS['db_server'],$GLOBALS['db_user'],$GLOBALS['db_passwd']) OR die(mysqli_error());
mysqli_select_db($db_link,$GLOBALS['db_name']) OR die(mysqli_error());

$query = "SELECT ac.id AS ac_id, ac.alloc_id,ac.type,a.type AS data_type,
		 lh.lock_id AS locks, l.type AS lock_types, l.embedded_in AS embedded_in,
		 ac.address - a.ptr AS offset, ac.size
	  FROM accesses AS ac
	  INNER JOIN allocations AS a ON a.id=ac.alloc_id
	  INNER JOIN locks_held AS lh ON lh.access_id=ac.id
	  INNER JOIN locks AS l ON l.id=lh.lock_id
	  ORDER BY ac.id
	  -- LIMIT 0,100";
$start = microtime();
$result = mysqli_query($db_link,$query,MYSQLI_USE_RESULT);
if ($result === false) {
	echo "query failed\n".mysqli_error($db_link)."\n";
	die();
}
$end = microtime();
echo "Query took " . ($end - $start) / 1000000 . " seconds\n";

$outfile = fopen($outfile_name,"w+");
if ($outfile === false) {
	mysqli_close($db_link);
	die("Cannot open " . $outfile_name . "\n");
}

$line = "ac_id" . $delimiter . "alloc_id" . $delimiter . "data_type" . $delimiter . "locks" . $delimiter . "locktypes" . $delimiter . "offset" . $delimiter . "size\n";
fwrite($outfile,$line);
$i = 0;
$k = 0;

$row = mysqli_fetch_assoc($result);
while ($row) {
	$ac_id = $row['ac_id'];
	$alloc_id = $row['alloc_id'];
	$data_type = $row['data_type'];
	$locks = array();
	$lock_types = array();
	$emebedded_in = array();

	do {
		$locks[] = $row['locks'];
		$lock_types[] = $row['lock_types'];
		$emebedded_in[] = $row['embedded_in'];
		$row = mysqli_fetch_assoc($result);
		$i++;
	} while ($row && $ac_id == $row['ac_id']);

	$line = $ac_id . $delimiter . $alloc_id . $delimiter . $data_type . $delimiter . implode($delimiter_locks,$locks) . $delimiter . implode($delimiter_locks,$lock_types) . $delimiter . implode($delimiter_locks,$embedded_in) . "\n";
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
