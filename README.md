# Parcial #1 de la clase de Sistemas operativos UNAL

## Roadmap (cosas adicionales a la funcionalidad solicitada por el problema que me gustaria añadir)
- [ ] Implementar RAII o alguna tecnica similar para la gestion de recursos
- [ ] Refactorizar el p3 para darle una estructura mas generica y mejor segregacion de responsabilidades
- [ ] Remover todo estado global de p1 y p3
- [ ] Convertir toda la logica de loggeo de una funciona a una macro
- [ ] Permitir parametros customizados de la misma manera que existen en p1 a p3 (llave del semaforo y shared memory)
- [ ] Convertir los guardias de las funciones a aserciones para tener una solucion mas idiomatica

## Instrucciones de uso

> Se recomienta usar NixOS como el sistema operativo del entorno de ejecucion o que este cuente con el gestor de paquetes Nix, esto debido a que el repositorio cuenta con una devShell de Nix la cual facilita y garantiza la ejecucion del programa ya que incluye todas las dependencias de compilacion y desarrollo
> Este devShell cuenta con los siguientes items:
> - GCC, Valgrind y GNU Make como herramientas de compilacion y desarrollo
> - Distribucion de Neovim configurada con CCLS como LSP para C/C++
> - Para acceder a la devShell del proyecto basta con ejecutar el siguiente comando: `nix develop` o `nix develop path:/root/path/of/repo`

Para compilar el proyecto se debe contar con GCC y GNU Make instalados en la maquina, para compilar basta con ejecutar el siguiente comando: `make` o `make compile`, esto creara una carpeta build donde se encontraran dos ejecutables: `p1.o` y `p3.o` que corresponden a los solicitados por el ejercicio
