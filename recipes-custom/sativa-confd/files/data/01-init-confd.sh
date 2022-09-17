#!/bin/sh

/bin/mkdir -p /persistent/config/confd-data

/bin/chown -R confd /persistent/config/confd-data
/bin/chgrp -R daemon /persistent/config/confd-data
