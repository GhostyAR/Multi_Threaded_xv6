FROM ubuntu:18.04

RUN apt update -y
RUN apt upgrade -y

RUN apt install -y curl git wget unzip unrar gcc clang zsh vim make qemu-system-x86


RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)"

RUN apt install -y sudo

RUN chsh -s $(which zsh)
WORKDIR /src

CMD ["zsh"]
