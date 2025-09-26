#!/bin/zsh

SCRIPT=${0:t}
PARENT=${0:A:h}
source $PARENT/common

DEPTH=1
HIDDEN=0
DIRECTORIES=()

print-usage() {
  echo "Usage: ${SCRIPT} [-ah] [--depth INT | --tree] [FILE ...]"
}

while [[ $# -gt 0 ]]; do
  case $1 in
    -h|--help)
      print-usage
      exit 0
      ;;
    --depth)
      DEPTH="$2"
      shift 2
      ;;
    --tree)
      DEPTH=$(getconf PATH_MAX /)
      shift
      ;;
    -a)
      HIDDEN=1
      shift
      ;;
    -*)
      echo "Unknown option: $1"
      print-usage
      exit 1
      ;;
    *)
      if [[ -e $1 ]]; then
        DIRECTORIES+=("$1")
      else
        echo "Error: '$1' does not exist"
        exit 1
      fi
      shift
      ;;
  esac
done

if [[ ${#DIRECTORIES[@]} -eq 0 ]]; then
  DIRECTORIES=(".")
fi

print-line() {

  local line="$1"
  local DEPTH="$2"
  local ARR="$3"

  local IX

  TARGET=""
  ICON=""
  COLOUR="$WHITE"

  if [[ -L $line ]]; then
    TARGET=${line:A}
    if [[ -d $TARGET ]]; then
      ICON=""
      COLOUR=$CYAN
    elif [[ -x $TARGET ]]; then
      ICON=""
      COLOUR=$GREEN
    elif [[ -f $TARGET ]]; then
      ICON=""
      COLOUR=$WHITE
    fi
  elif [[ -d $line ]]; then
    ICON=""
    COLOUR=$BLUE
  elif [[ -x $line ]]; then
    ICON=""
    COLOUR=$GREEN
  elif [[ -f $line ]]; then
    ICON=""
    COLOUR=$WHITE
  fi

  if [[ ${line:A} == ${PWD} ]]; then
    COLOUR=$YELLOW_B
  fi

  char="${GREY}"
  for ((IX=1; IX<DEPTH; IX++)); do
    if (( ARR[$IX] )); then
      char+="│  "
    else
      char+="   "
    fi
  done
  if [[ $DEPTH -gt 0 ]]; then
    if (( $ARR[$DEPTH] )); then
      char+="├─ "
    else
      char+="└─ "
    fi
  fi
  char+="${NC}"

  if [[ -z $TARGET ]]; then
    echo "${char}${COLOUR}${ICON} ${line:t}${NC}"
  else
    echo "${char}${COLOUR}${ICON} ${line:t} ${NC}${COLOUR} ${TARGET/#$HOME/~}${NC}"
  fi

}

print-dir() {

  local FILE="$1"
  local DEPTH="$2"
  local MAXDEPTH="$3"
  local HIDDEN="$4"
  local ARR="$5"

  local IX;
  local FILES;

  print-line $FILE $DEPTH $ARR

  if [[ ! -d $FILE ]] || [[ $DEPTH -ge $MAXDEPTH ]]; then
    return
  fi

  setopt nullglob
  if (( $HIDDEN )); then
    FILES=($FILE/.* $FILE/*)
    FILES=(${FILES[@]:#*/.})
    FILES=(${FILES[@]:#*/..})
  else
    FILES=($FILE/*)
  fi
  unsetopt nullglob

  for ((IX=1; IX<=${#FILES[@]}; IX++)); do
    FILE=${FILES[$IX]}
    if (( IX == ${#FILES[@]} )); then
      ARR[$((DEPTH + 1))]=0
    else
      ARR[$((DEPTH + 1))]=1
    fi
    print-dir "$FILE" $(($DEPTH + 1)) $MAXDEPTH $HIDDEN $ARR
  done

}

for DIR in "${DIRECTORIES[@]}"; do
  ARR=()
  for ((i=1; i<=MAXDEPTH; i++)); do
    ARR+=1;
  done
  print-dir ${DIR:A} 0 $DEPTH $HIDDEN $ARR
done
