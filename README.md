tsar module for php-fpm
=======================


1. git clone https://github.com/alibaba/tsar.git
1. git clone https://github.com/guoxiaod/tsar-php-fpm.git
1. cd tsar && sudo make tsardevel
1. cd ../tsar-php-fpm && make
1. sudo make install
1. tsar --php-fpm -i 1 -l 
1. if there are many php-fpm instances, we can tell tsar by this environment 
   export PHP_FPM_TSAR_HOST=tcp://127.0.0.1:9000/,http://127.0.0.1/,unix:/var/run/php-fpm/www.sock
