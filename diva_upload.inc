<?php

function diva_get_queue() {
    $queue = DrupalQueue::get("diva_image_processing");
    $queue->createQueue();
    return $queue;
}

function diva_clear_queue() {
    $queue = DrupalQueue::get("diva_image_processing");
    $queue->deleteQueue();
}

function diva_insert_into_queue($queue_item) {
    $q = diva_get_queue();

    $dest = $queue_item['path'];
    if($handle = opendir($dest)) {
        while (false !== ($entry = readdir($handle))) {
            if(is_dir($dest . "/" . $entry) || _startswith($entry, ".")) {
                continue;
            }
            $q->createItem($queue_item);
        }
    }

}

function diva_cron_queue_info() {
    return array(
        'diva_image_processing' => array(
            'worker callback' => 'diva_process_images',
            'time' => variable_get('diva_time_per_cron_run', 120)
        )
    );
}

function diva_process_images($item) {
    // takes in a queue item.
    $img_size = getimagesize($item['path']);
    $img_wid = $img_size[0];
    $img_hei = $img_size[1];

    $max_zoom = get_max_zoom_level($img_wid, $img_hei, $til_wid, $til_hei);
    $lowest_max_zoom = ($lowest_max_zoom > $max_zoom || $lowest_max_zoom == 0) ? $max_zoom : $lowest_max_zoom;
}

function _create_image_cache($img_file) {
    

}