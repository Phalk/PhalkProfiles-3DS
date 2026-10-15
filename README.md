# Como usar o Phalk Profiles no 3DS

1. Copie `PhalkProfiles-3DS.3dsx` para a pasta `/3ds/PhalkProfiles/` do cartão SD.
2. Abra o app pelo Homebrew Launcher.
3. Escolha **Set username** e **Set password** para informar sua conta do Phalk Profiles.
4. Escolha **Create JSON** para salvar seu histórico no cartão SD ou **Create and upload** para salvar e enviar ao site. Para enviar, conecte o 3DS ao Wi-Fi.

O arquivo gerado fica em `/3ds/PhalkProfiles/export.json`. O app lê o Registro de Atividades do próprio 3DS; você não precisa do Activity Log++.

Os nomes dos jogos são consultados localmente pelo TitleID no banco `romfs:/3ds_db.json`, incluído no aplicativo. O nome encontrado também é gravado no campo `name` do `export.json`.

Se encontrar datas futuras, o app mostra o nome do jogo e as datas de início e fim. Use **esquerda/direita** para escolher ano, mês ou dia de cada data; use **cima/baixo** para alterar o valor. Pressione **A** para salvar ou **B** para cancelar. O JSON só é criado depois que todas as datas futuras forem corrigidas. As correções ficam em `/3ds/PhalkProfiles/date_corrections.txt`; o registro da NAND não é alterado. Correções feitas na tela antiga precisam ser salvas de novo.

Use cima/baixo para navegar, **A** para selecionar e **START** para sair.

**Atenção:** o envio usa HTTP sem criptografia. Sua senha e seu histórico trafegam pela rede sem proteção.
