Agora voce atuar'a como desenvolvedor senior de aplicacoes otimizadas em C++ com Raylib + yt-dlp + mpv. 

Voce contruir'a uma aplicacao para ser executada em um Armbian Debian (Trexie) + XOrg em modo limitado(otimizado). O hardware possui 16GB ROM (FLASH) / 2GB RAM (DDR), Chip RK3229. Ou seja, muito limitado. 

A aplicacao estar'a voltada a um sistema de Kioski, ou seja ao ligar na tomada, aparecer'a a execucao do binario que ser'a desenvolvido. 

## Necessidades do projeto: 
1. Replicar vers~ao que est'a aplicado para Web.
    > Analise a seguinte URL: https://esustv.jfbatl.com.br/display
    > Colete todas informacoes de design, estilo e cores
    > Replique mesmo layout, fluxo de aplicacacao e foque nos videos
2. Os videos nao devem ser renderizados como iframe
    > A aplicacao dever'a fazer um Web Scrapping procurando pelos videos
    > Foque na otimizacao, porem nao afete a qualidade do video
    > Todos as replicacoes sao autorizadas pelo dono do sistema
3. Ao iniciar aplicacao o layout devera estar fixo em 25% de width e height 100%
4. Otimizacao
    > Reduza o uso de mem'oria RAM ao maximo, foque em estilo e qualidade de otimizacao
5. Estrutura
    > Organize nas pastas j'a criadas no projeto, organizacao. 

## Necessidades do Repo
1. Crie estrutura de memoria
    > `README.md`
    > `docs/memory`
    > `docs/memory/architecture.md`
    > `docs/memory/frontend-contract.md`
    > `docs/memory/known-issues.md`
    > `docs/memory/session-handoff.md`


## Leitura Obrigatoria

Antes de alterar codigo, leia nesta ordem:

1. `README.md`
2. `docs/memory/session-handoff.md`
3. `docs/memory/known-issues.md`
4. O contrato da area de trabalho:
   - frontend: `docs/memory/frontend-contract.md`
   - arquitetura/infra: `docs/memory/architecture.md`

Portanto, execute e implemente.