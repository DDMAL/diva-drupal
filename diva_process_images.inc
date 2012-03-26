<?php

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
    if($handle = opendir($dest)) {
        while (false !== ($entry = readdir($handle))) {
            if(is_dir($dest . "/" . $entry) || _startswith($entry, ".")) {
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
        }

        db_update('diva_data')
            ->fields(array(
                'total_images' => $i,
                'image_path' => $dest
            ))
            ->condition('divaid', $queue_item['dvid'])
            ->execute();

        // set up a running total for the number of images processed
        variable_set("diva_proc_num_images_" . $queue_item['dvid'] . "_step_1", 0);
    }
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

    db_update("diva_images")
            ->fields(array(
                'image_width' => $img_wid,
                'image_height' => $img_hei,
                'image_max_zoom' => $img_zoom
            ))
            ->condition("imid", $item['image_id'])
            ->execute();

    $proc = variable_get("diva_proc_num_images_" . $item['dvid'] . "_step_1", 0);
    $proc++;
    if($proc == $ddata['total_images']) {
        // set step 1 complete
        $dims = db_query("SELECT MAX(image_height) AS maxh, MAX(image_width) AS maxw, 
                                 MAX(image_max_zoom) AS maxz, MIN(image_max_zoom) AS minz,
                                 AVG(image_height) AS avgh, AVG(image_width) AS avgw,
                                 SUM(image_height) AS thei, SUM(image_width) AS twid
                        FROM {diva_images} 
                        WHERE divaid = (:dvid) LIMIT 0,1", array(':dvid' => $item['dvid']))
                 ->fetchAssoc();

        $lowest_max_zoom = $dims['minz'];

        db_update("diva_data")
            ->fields(array(
                'global_image_dims' => serialize($dims)
            ))
            ->condition("divaid", $item['dvid'])
            ->execute();

        // queue the images for further processing
        $imgs = db_select("diva_images", "d")
            ->fields('d')
            ->condition("divaid", $item['dvid'])
            ->execute();
        
        $q2 = diva_get_image_queue_step_2();
        while($img = $imgs->fetchAssoc()) {
            $qitem = array(
                'imid' => $img['imid'],
                'divaid' => $img['divaid']
            );
            $q2->createItem($qitem);
        }
    } else {
        variable_set('diva_proc_num_images_' . $item['dvid'] . "_step_1", $proc);
    }
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
                        'global_image_dims',
                        'tile_width',
                        'tile_height'
                    ))
                    ->condition("divaid", $item['divaid'])
                    ->execute()
                    ->fetchAssoc();
    
    $dims = unserialize($image_data['global_image_dims']);

    $imgs = db_select("diva_images", 'd')
            ->fields('d')
            ->condition("divaid", $item['divaid'])
            ->execute();

    while($img = $imgs->fetchAssoc()) {
        $iarr = array();
        for ($i=0; $i <= $dims['minz']; $i++) { 
                $h = _diva_incorporate_zoom($img['image_height'], $dims['minz'] - $i);
                $w = _diva_incorporate_zoom($img['image_width'], $dims['minz'] - $i);
                $c = ceil($w / $image_data['tile_width']);
                $r = ceil($h / $image_data['tile_height']);
                $m_z = $img['image_max_zoom'];
                $fn = $img['image'];
                $iarr[$i] = array(
                    'c' => $c,
                    'r' => $r,
                    'w' => $w,
                    'h' => $h,
                    'm_z' => $m_z,
                    'fn' => $fn
                );
        }
        db_update("diva_images")
            ->fields(array(
                'zoom_levels' => serialize($iarr)
            ))
            ->condition('imid', $img['imid'])
            ->execute();
    }
}

function diva_is_done_step_1() {

}

function diva_is_done_step_2() {

}
