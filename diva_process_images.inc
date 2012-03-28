<?php
/**
 * @file
 * A module for working with diva.js
 */

function diva_get_image_queue_step_1() {
    $queue = DrupalQueue::get("diva_image_queue_step_1");
    $queue->createQueue();
    return $queue;
}

function diva_get_image_queue_step_2() {
    $queue = DrupalQueue::get("diva_image_queue_step_2");
    $queue->createQueue();
    return $queue;
}

function diva_delete_image_queue_step_1() {
    $queue = DrupalQueue::get("diva_image_queue_step_1");
    $queue->deleteQueue();
}

function diva_delete_image_queue_step_2() {
    $queue = DrupalQueue::get("diva_image_queue_step_2");
    $queue->deleteQueue();
}


function diva_insert_image_queue_step_1($queue_item) {
    $q = diva_get_image_queue_step_1();

    $dest = $queue_item['path'];
    $i = 0;
    if ($handle = opendir($dest)) {
        while (false !== ($entry = readdir($handle))) {
            if (is_dir($dest . "/" . $entry) || _startswith($entry, ".")) {
                continue;
            }
            $imid = db_insert('diva_images')
                ->fields(array(
                    'divaid' => $queue_item['dvid'],
                    'nid' => $queue_item['nid'],
                    'image' => $dest . "/" . $entry,
                ))
                ->execute();

            $queue_item['image_id'] = $imid;
            $queue_item['image_filename'] = $entry;
            $q->createItem($queue_item);
            $i++;

            unset($imid);
        }

        $upd = db_update('diva_data')
                ->fields(array(
                    'total_images' => $i,
                    'image_path' => $dest
                ))
                ->condition('divaid', $queue_item['dvid'])
                ->execute();

        // set up a running total for the number of images processed
        variable_set("diva_proc_num_images_" . $queue_item['dvid'] . "_step_1", 0);
        unset($upd);
    }

    unset($handle);
}

function diva_insert_image_queue_step_2($queue_item) {

}


/* Implements hook_cron_queue_info() */
function diva_cron_queue_info() {
    return array(
        'diva_image_queue_step_1' => array(
            'worker callback' => 'diva_process_images_step_1',
            'time' => variable_get('diva_time_per_cron_run', 120)
        ),
        'diva_image_queue_step_2' => array(
            'worker callback' => 'diva_process_images_step_2',
            'time' => variable_get('diva_time_per_cron_run', 120)
        ),

    );
}

function diva_process_images_step_1($item) {
    // takes in a queue item.
    $ddata = db_select("diva_data", "d")
            ->fields("d", array(
                "total_images",
                "tile_width",
                "tile_height"
            ))
            ->condition("divaid", $item['dvid'])
            ->execute()
            ->fetchAssoc();

    $img_size = getimagesize($item['path'] . "/" . $item['image_filename']);
    $img_wid = $img_size[0];
    $img_hei = $img_size[1];

    $img_zoom = _diva_get_max_zoom_level($img_wid, $img_hei, $ddata['tile_width'], $ddata['tile_height']);

    $upd = db_update("diva_images")
            ->fields(array(
                'image_width' => $img_wid,
                'image_height' => $img_hei,
                'image_max_zoom' => $img_zoom
            ))
            ->condition("imid", $item['image_id'])
            ->execute();
    unset($upd);

    $proc = variable_get("diva_proc_num_images_" . $item['dvid'] . "_step_1", 0);
    $proc++;
    if ($proc == $ddata['total_images']) {
        // set step 1 complete
        $lmz = db_query("SELECT MIN(image_max_zoom) AS minz FROM {diva_images}
                            WHERE divaid = (:dvid) LIMIT 0,1", array(':dvid' => $item['dvid']))
                ->fetchAssoc();

        $lowest_max_zoom = $lmz['minz'];

        $upd = db_update("diva_data")
                ->fields(array(
                    'lowest_max_zoom' => $lowest_max_zoom
                ))
                ->condition("divaid", $item['dvid'])
                ->execute();

        unset($upd);

        // queue the images for further processing
        $imgs = db_select("diva_images", "d")
                ->fields('d')
                ->condition("divaid", $item['dvid'])
                ->execute();

        $q2 = diva_get_image_queue_step_2();
        while ($img = $imgs->fetchAssoc()) {
            $qitem = array(
                'imid' => $img['imid'],
                'divaid' => $img['divaid']
            );
            $q2->createItem($qitem);
        }

        unset($lmz);
        unset($q2);
        unset($imgs);

    }
    else {
        variable_set('diva_proc_num_images_' . $item['dvid'] . "_step_1", $proc);
    }

    unset($ddata);
}

function _diva_get_max_zoom_level($i_wid, $i_hei, $t_wid, $t_hei) {
    $largest_dim = ($i_wid > $i_hei) ? $i_wid : $i_hei;
    $t_dim = ($i_wid > $i_hei) ? $t_wid : $t_hei;
    $zoom_levels = ceil(log(($largest_dim + 1)/($t_dim + 1), 2));
    return intval($zoom_levels);
}

function _diva_incorporate_zoom($img_dim, $zoom_difference) {
    return $img_dim / pow(2, $zoom_difference);
}

function diva_process_images_step_2($item) {
    // takes in a queue item.
    $image_data = db_select("diva_data", 'd')
                    ->fields('d', array(
                        'lowest_max_zoom',
                        'tile_width',
                        'tile_height',
                        'total_images',
                        'image_path'
                    ))
                    ->condition("divaid", $item['divaid'])
                    ->execute()
                    ->fetchAssoc();

    $imgs = db_select("diva_images", 'd')
            ->fields('d')
            ->condition("divaid", $item['divaid'])
            ->execute();

    $totals = array();
    for ($i=0; $i <= $image_data['lowest_max_zoom']; $i++) { 
        $totals[$i] = array(
            't_wid' => array(),
            't_hei' => array(),
            'ratio' => array(),
            'pgs' => array()
        );
    }

    while ($img = $imgs->fetchAssoc()) {
        _diva_image_dimensions($img, $image_data, $totals);
    }

    $collection_totals = array();
    foreach($totals as $zoom => $tot) {
        $tot_w = array_sum($tot['t_wid']);
        $tot_h = array_sum($tot['t_hei']);
        $collection_totals[$zoom] = array();
        $collection_totals[$zoom]['dims'] = array(
            't_wid' => $tot_w,
            't_hei' => $tot_h,
            'max_w' => max($tot['t_wid']),
            'max_h' => max($tot['t_hei']),
            'a_wid' => $tot_w / count($tot['t_wid']),
            'a_hei' => $tot_h / count($tot['t_hei']),
            'max_ratio' => max($tot['ratio']),
            'min_ratio' => min($tot['ratio']),
        );

        $collection_totals[$zoom]['pgs'] = $tot['pgs'];
        $collection_totals[$zoom]['max_zoom'] = $image_data['lowest_max_zoom'];
        $collection_totals[$zoom]['item_title'] = $item['divaid'];
    }

    db_update("diva_data")
        ->fields(array(
            'collection_totals' => serialize($collection_totals)
        ))
        ->condition("divaid", $item['divaid'])
        ->execute();
}

function _diva_image_dimensions($img_row, $img_data, &$totals) {
    $lmz = $img_data['lowest_max_zoom'];
    for ($i=0; $i <= $img_data['lowest_max_zoom']; $i++) {
        $h = _diva_incorporate_zoom($img_row['image_height'], $lmz - $i);
        $w = _diva_incorporate_zoom($img_row['image_width'], $lmz - $i);
        $c = ceil($w / $img_data['tile_width']);
        $r = ceil($h / $img_data['tile_height']);
        $m_z = $img_row['image_max_zoom'];
        $fn = $img_row['image'];
        $pgs = array(
            'c' => $c,
            'r' => $r,
            'w' => $w,
            'h' => $h,
            'm_z' => $m_z,
            'fn' => $fn
        );

        array_push($totals[$i]['t_wid'], $w);
        array_push($totals[$i]['t_hei'], $h);
        array_push($totals[$i]['ratio'], ($h / $w));
        array_push($totals[$i]['pgs'], $pgs);
    }
    // db_update("diva_images")
    //     ->fields(array(
    //         'zoom_levels' => serialize($iarr)
    //     ))
    //     ->condition('imid', $img_row['imid'])
    //     ->execute();
}

// We break this out here so we can re-cache this without having to re-process them.
function diva_cache_images($divaid) {
    $coll = db_select("diva_data", "d")
                    ->fields("d", array(
                        'lowest_max_zoom',
                        'collection_totals'
                    ))
                    ->condition('divaid', $divaid)
                    ->execute()
                    ->fetchAssoc();

    $collection = unserialize($coll['collection_totals']);

    foreach($collection as $zoom => $data) {
        $cachekey = $divaid . "-" . $zoom;
        cache_set($cachekey, $data, 'cache_diva');
    }
}


function diva_is_done_step_1() {

}

function diva_is_done_step_2() {

}
