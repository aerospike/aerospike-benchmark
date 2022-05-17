#!/bin/bash

if [ -z "${1}" ]; then
	echo "Please specify the directory for the Python environment"
	exit 1
fi

if ! command -v virtualenv &> /dev/null
then
	sudo python3 -m pip install pipenv
fi

if [ ! -d "${1}" ]; then
	echo "Creating Python environment in \"${1}\""
	virtualenv "${1}"
	. "${1}"/bin/activate
	pip install -r requirements.txt
else
	. "${1}"/bin/activate
fi

pytest src/test/integration

