# Teste do som e do Ctrl+C no G470

Imagem: `entrega/v2-g470-som/saguios.img` (grave no pendrive como as outras).

## Ctrl+C

1. No Terminal, rode `contador 100`.
2. Aperte **Ctrl+C**: deve aparecer `^C` e o prompt volta.

## Som

1. Toque a música: `tocar /usb/musica.wav`.
2. Três resultados possíveis:
   - **toca com som**: o kernel conseguiu partir o fluxo de som de novo. Ótimo!
   - **"a placa de som não está tocando"** depois de uns 3 segundos: o
     programa desiste sozinho (não trava mais);
   - qualquer outra coisa: anote.
3. Em todos os casos, grave o diário do som no pendrive e mande o arquivo:

   ```
   registro HDA > /usb/som.txt
   ```

O arquivo mostra se o fluxo de áudio andou, se foi reprogramado e o estado
de cada pino do chip (alto-falante, fone, microfone).
