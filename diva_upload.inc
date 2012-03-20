<?php

function diva_create_queue() {
    $queue = DrupalQueue::get("diva_image_processing");
    $queue->createQueue();
    
}


function _create_image_cache($img_file) {
    

}