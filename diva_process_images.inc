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

    $images = array();
    $pgs = array();
    $til_wid = $image_data['tile_width'];
    $lowest_max_zoom = intval($image_data['lowest_max_zoom']);
    $i = 0;

    while($img = $imgs->fetchAssoc()) {
        $img_wid = $img['image_width'];
        $img_hei = $img['image_height'];
        $max_zoom = intval($img['image_max_zoom']);

        $img_fn = substr($img['image'], strrpos($img['image'], '/') + 1);

        $images[$i] = array(
            'mx_h' => $img_hei,
            'mx_w' => $img_wid,
            'mx_z' => $max_zoom,
            'fn' => $img_fn
        );
        $i++;
    }

    $num_pages = $image_data['total_images'];
    $max_ratio = $min_ratio = 0;
    $t_wid = array_fill(0, $lowest_max_zoom + 1, 0);
    $t_hei = array_fill(0, $lowest_max_zoom + 1, 0);
    $mx_h = array_fill(0, $lowest_max_zoom + 1, 0);
    $mx_w = array_fill(0, $lowest_max_zoom + 1, 0);
    $a_wid = array();
    $a_hei = array();

    for ($i = 0; $i < count($images) - 1; $i++) { 
        if (array_key_exists($i, $images)) {
            $page_data = array();
            for ($j=0; $j < $lowest_max_zoom; $j++) { 
                $h = _diva_incorporate_zoom($images[$i]['mx_h'], $lowest_max_zoom - $j);
                $w = _diva_incorporate_zoom($images[$i]['mx_w'], $lowest_max_zoom - $j);
                $c = ceil($w / $til_wid);
                $r = ceil($h / $til_wid);
                $page_data[] = array(
                    'c' => $c,
                    'r' => $r,
                    'h' => $h,
                    'w' => $w
                );
                $t_wid[$j] += $w;
                $t_hei[$j] += $h;
                $mx_h[$j] = ($h > $mx_h[$j]) ? $h : $mx_h[$j];
                $mx_w[$j] = ($w > $mx_w[$j]) ? $w : $mx_w[$j];
            }

            $m_z = $images[$i]['mx_z'];
            $fn = $images[$i]['fn'];
            $pgs[] = array(
                'd' => $page_data,
                'm' => $m_z,
                'f' => $fn
            );

            $ratio = $h / $w;
            $max_ratio = ($ratio > $max_ratio) ? $ratio : $max_ratio;
            $min_ratio = ($ratio < $min_ratio || $min_ratio == 0) ? $ratio : $min_ratio;
        }
    }

    for ($j=0; $j < $lowest_max_zoom; $j++) { 
        $a_wid[] = $t_wid[$j] / $num_pages;
        $a_hei[] = $t_hei[$j] / $num_pages;
    }

    $dims = array(
        'a_wid' => $a_wid,
        'a_hei' => $a_hei,
        'max_w' => $mx_w,
        'max_h' => $mx_h,
        'max_ratio' => $max_ratio,
        'min_ratio' => $min_ratio,
        't_hei' => $t_hei,
        't_wid' => $t_wid,
    );

    $title = '';
    $data = array(
        'item_title' => $title,
        'dims' => $dims,
        'max_zoom' => $lowest_max_zoom,
        'pgs' => $pgs
    );

    db_update("diva_data")
        ->fields(array(
            'collection_totals' => serialize($data)
        ))
        ->condition("divaid", $item['divaid'])
        ->execute();
}

// We break this out here so we can re-cache this without having to re-process them.
function diva_cache_images($divaid) {
    $coll = db_select("diva_data", "d")
                    ->fields("d", array(
                        'collection_totals'
                    ))
                    ->condition('divaid', $divaid)
                    ->execute()
                    ->fetchAssoc();

    $collection = unserialize($coll['collection_totals']);
    $cachekey = 'diva-' . $divaid;
    cache_set($cachekey, $collection, 'cache_diva');
}


function diva_is_done_step_1() {

}

function diva_is_done_step_2() {

}
