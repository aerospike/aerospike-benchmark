#!/bin/bash

if [ -z "${1}" ]; then
	echo "Please specify the directory for the Python environment"
	exit 1
fi

PYTHON3="${PYTHON3:-python3}"

if ! command -v "${PYTHON3}" &> /dev/null
then
	echo "python3 is required for the integration tests but was not found in PATH." >&2
	echo "Install Python 3, or set PYTHON3=/path/to/python3." >&2
	exit 1
fi

if [ ! -d "${1}" ]; then
	echo "Creating Python environment in \"${1}\""
	if command -v virtualenv &> /dev/null
	then
		virtualenv -p "${PYTHON3}" "${1}" || exit 1
	else
		"${PYTHON3}" -m venv "${1}" || exit 1
	fi
	. "${1}"/bin/activate
	pip install -r requirements.txt || exit 1
else
	. "${1}"/bin/activate
fi

pytest -v src/test/integration
